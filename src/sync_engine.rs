use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use tokio::fs::OpenOptions;
use tokio::io::{AsyncSeekExt, AsyncWriteExt, SeekFrom};
use walkdir::WalkDir;

use crate::network::Message;
use crate::utils;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileEntry {
    pub path: String,
    pub size: u64,
    pub modified_ts: i64,
    pub sha256: String,
}

#[derive(Debug, Clone)]
pub enum LocalChange {
    Changed(String),
    Deleted(String),
}

#[derive(Clone)]
pub struct SyncEngine {
    root: PathBuf,
}

impl SyncEngine {
    pub fn new(root: PathBuf) -> Self {
        Self { root }
    }

    pub fn root(&self) -> &Path {
        &self.root
    }

    pub fn abs_path(&self, rel: &str) -> Result<PathBuf> {
        utils::safe_join(&self.root, rel)
    }

    pub async fn scan_manifest(&self) -> Result<Vec<FileEntry>> {
        let root = self.root.clone();
        tokio::task::spawn_blocking(move || -> Result<Vec<FileEntry>> {
            let mut entries = Vec::new();
            for e in WalkDir::new(&root).follow_links(false) {
                let e = e?;
                if !e.file_type().is_file() {
                    continue;
                }

                let abs = e.path().to_path_buf();
                if abs
                    .extension()
                    .and_then(|v| v.to_str())
                    .map(|ext| ext == "part")
                    .unwrap_or(false)
                {
                    continue;
                }

                let rel = abs
                    .strip_prefix(&root)
                    .context("strip_prefix failed")?
                    .to_path_buf();
                let rel_str = utils::normalize_relative_path(&rel)?;

                let meta = std::fs::metadata(&abs)?;
                let modified_ts = meta
                    .modified()
                    .map(utils::system_time_to_unix_secs)
                    .unwrap_or(0);

                let hash = {
                    use sha2::{Digest, Sha256};
                    let mut file = std::fs::File::open(&abs)?;
                    let mut hasher = Sha256::new();
                    let mut buf = [0u8; 64 * 1024];
                    loop {
                        let n = std::io::Read::read(&mut file, &mut buf)?;
                        if n == 0 {
                            break;
                        }
                        hasher.update(&buf[..n]);
                    }
                    format!("{:x}", hasher.finalize())
                };

                entries.push(FileEntry {
                    path: rel_str,
                    size: meta.len(),
                    modified_ts,
                    sha256: hash,
                });
            }
            Ok(entries)
        })
        .await
        .context("manifest scan task failed")?
    }

    pub async fn build_file_entry(&self, rel: &str) -> Result<Option<FileEntry>> {
        let abs = self.abs_path(rel)?;
        let meta = match tokio::fs::metadata(&abs).await {
            Ok(m) => m,
            Err(_) => return Ok(None),
        };
        if !meta.is_file() {
            return Ok(None);
        }

        let modified_ts = meta
            .modified()
            .map(utils::system_time_to_unix_secs)
            .unwrap_or(0);
        let sha256 = utils::sha256_file(&abs).await?;

        Ok(Some(FileEntry {
            path: rel.to_string(),
            size: meta.len(),
            modified_ts,
            sha256,
        }))
    }

    pub async fn reconcile_manifest(&self, remote: &[FileEntry]) -> Result<Vec<Message>> {
        let local = self.scan_manifest().await?;
        let mut local_map = HashMap::new();
        for f in local {
            local_map.insert(f.path.clone(), f);
        }

        let mut out = Vec::new();
        for r in remote {
            match local_map.get(&r.path) {
                None => {
                    let offset = self.resume_offset(&r.path).await?;
                    out.push(Message::NeedFile {
                        path: r.path.clone(),
                        offset,
                    });
                }
                Some(l) => {
                    if should_replace_local(l, r) {
                        let offset = self.resume_offset(&r.path).await?;
                        out.push(Message::NeedFile {
                            path: r.path.clone(),
                            offset,
                        });
                    }
                }
            }
        }

        Ok(out)
    }

    async fn resume_offset(&self, rel: &str) -> Result<u64> {
        let part = self.part_path(rel)?;
        let size = match tokio::fs::metadata(&part).await {
            Ok(m) => m.len(),
            Err(_) => 0,
        };
        Ok(size)
    }

    fn part_path(&self, rel: &str) -> Result<PathBuf> {
        let abs = self.abs_path(rel)?;
        let file_name = abs
            .file_name()
            .and_then(|s| s.to_str())
            .ok_or_else(|| anyhow!("invalid file name"))?;
        Ok(abs.with_file_name(format!("{file_name}.syncflow.part")))
    }

    pub async fn apply_delete(&self, rel: &str) -> Result<()> {
        let abs = self.abs_path(rel)?;
        let _ = tokio::fs::remove_file(&abs).await;
        let part = self.part_path(rel)?;
        let _ = tokio::fs::remove_file(&part).await;
        Ok(())
    }

    pub async fn handle_file_chunk(
        &self,
        path: &str,
        offset: u64,
        data: &[u8],
        done: bool,
        file_size: u64,
        modified_ts: i64,
        expected_sha: Option<&str>,
    ) -> Result<()> {
        let final_path = self.abs_path(path)?;
        if let Some(parent) = final_path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .with_context(|| format!("create_dir_all failed: {}", parent.display()))?;
        }

        let part_path = self.part_path(path)?;
        let mut file = OpenOptions::new()
            .create(true)
            .write(true)
            .read(true)
            .open(&part_path)
            .await
            .with_context(|| format!("failed to open part file: {}", part_path.display()))?;

        file.seek(SeekFrom::Start(offset))
            .await
            .context("seek part file failed")?;
        file.write_all(data).await.context("write part failed")?;
        file.flush().await.context("flush part failed")?;

        if !done {
            return Ok(());
        }

        let actual_size = tokio::fs::metadata(&part_path)
            .await
            .context("metadata part failed")?
            .len();
        if actual_size != file_size {
            return Ok(());
        }

        if let Some(expected_sha) = expected_sha {
            let actual_sha = utils::sha256_file(&part_path).await?;
            if actual_sha != expected_sha {
                return Err(anyhow!("sha256 mismatch for {}", path));
            }
        }

        tokio::fs::rename(&part_path, &final_path)
            .await
            .with_context(|| {
                format!(
                    "rename part to final failed: {} -> {}",
                    part_path.display(),
                    final_path.display()
                )
            })?;

        #[cfg(unix)]
        {
            let _ = modified_ts;
        }

        Ok(())
    }

    pub async fn local_change_to_message(&self, change: LocalChange) -> Result<Option<Message>> {
        match change {
            LocalChange::Deleted(path) => Ok(Some(Message::DeleteNotice { path })),
            LocalChange::Changed(path) => {
                let entry = self.build_file_entry(&path).await?;
                Ok(entry.map(|entry| Message::FileChanged { entry }))
            }
        }
    }
}

fn should_replace_local(local: &FileEntry, remote: &FileEntry) -> bool {
    if local.sha256 == remote.sha256 {
        return false;
    }
    if remote.modified_ts > local.modified_ts {
        return true;
    }
    if remote.modified_ts < local.modified_ts {
        return false;
    }
    remote.sha256 > local.sha256
}
