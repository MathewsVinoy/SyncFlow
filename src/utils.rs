use anyhow::{anyhow, Context, Result};
use sha2::{Digest, Sha256};
use std::path::{Component, Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

pub fn system_time_to_unix_secs(time: SystemTime) -> i64 {
    time.duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

pub fn normalize_relative_path(path: &Path) -> Result<String> {
    if path.is_absolute() {
        return Err(anyhow!("expected relative path, got absolute path"));
    }

    let mut cleaned = PathBuf::new();
    for c in path.components() {
        match c {
            Component::Normal(p) => cleaned.push(p),
            Component::CurDir => {}
            _ => return Err(anyhow!("invalid relative path component")),
        }
    }

    let s = cleaned
        .to_str()
        .context("path is not valid UTF-8")?
        .replace('\\', "/");

    if s.is_empty() {
        return Err(anyhow!("empty relative path"));
    }

    Ok(s)
}

pub fn safe_join(root: &Path, rel: &str) -> Result<PathBuf> {
    let rel_path = Path::new(rel);
    let cleaned = normalize_relative_path(rel_path)?;
    Ok(root.join(cleaned))
}

pub async fn sha256_file(path: &Path) -> Result<String> {
    let path = path.to_path_buf();
    tokio::task::spawn_blocking(move || {
        let mut file = std::fs::File::open(&path)
            .with_context(|| format!("failed to open for hashing: {}", path.display()))?;
        let mut hasher = Sha256::new();
        let mut buf = [0u8; 64 * 1024];

        loop {
            let n = std::io::Read::read(&mut file, &mut buf)
                .with_context(|| format!("failed while hashing: {}", path.display()))?;
            if n == 0 {
                break;
            }
            hasher.update(&buf[..n]);
        }

        Ok::<_, anyhow::Error>(format!("{:x}", hasher.finalize()))
    })
    .await
    .context("hash task failed")?
}
