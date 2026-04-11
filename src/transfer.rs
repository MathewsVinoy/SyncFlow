use anyhow::{Context, Result};
use tokio::fs::File;
use tokio::io::{AsyncReadExt, AsyncSeekExt, SeekFrom};
use tokio::sync::mpsc;

use crate::network::Message;
use crate::sync_engine::SyncEngine;
use crate::utils;

pub const CHUNK_SIZE: usize = 256 * 1024;

pub async fn send_file_chunks(
    engine: &SyncEngine,
    rel_path: &str,
    offset: u64,
    tx: mpsc::UnboundedSender<Message>,
) -> Result<()> {
    let abs = engine.abs_path(rel_path)?;
    let meta = tokio::fs::metadata(&abs)
        .await
        .with_context(|| format!("metadata failed for {}", abs.display()))?;
    if !meta.is_file() {
        return Ok(());
    }

    let total_size = meta.len();
    let modified_ts = meta
        .modified()
        .map(utils::system_time_to_unix_secs)
        .unwrap_or(0);
    let hash = utils::sha256_file(&abs).await?;

    let mut file = File::open(&abs)
        .await
        .with_context(|| format!("open failed for {}", abs.display()))?;

    file.seek(SeekFrom::Start(offset))
        .await
        .with_context(|| format!("seek failed for {}", abs.display()))?;

    let mut cursor = offset;
    let mut buf = vec![0u8; CHUNK_SIZE];

    loop {
        let n = file
            .read(&mut buf)
            .await
            .with_context(|| format!("read failed for {}", abs.display()))?;
        if n == 0 {
            let _ = tx.send(Message::FileChunk {
                path: rel_path.to_string(),
                offset: cursor,
                data: Vec::new(),
                done: true,
                file_size: total_size,
                modified_ts,
                sha256: Some(hash.clone()),
            });
            break;
        }

        cursor += n as u64;
        let done = cursor >= total_size;

        let _ = tx.send(Message::FileChunk {
            path: rel_path.to_string(),
            offset: cursor - n as u64,
            data: buf[..n].to_vec(),
            done,
            file_size: total_size,
            modified_ts,
            sha256: if done { Some(hash.clone()) } else { None },
        });

        if done {
            break;
        }
    }

    Ok(())
}
