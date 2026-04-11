use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};

use crate::sync_engine::FileEntry;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Hello {
    pub device_name: String,
    pub token: String,
    pub node_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Message {
    Hello(Hello),
    ManifestRequest,
    ManifestResponse { entries: Vec<FileEntry> },
    FileChanged { entry: FileEntry },
    DeleteNotice { path: String },
    NeedFile { path: String, offset: u64 },
    FileChunk {
        path: String,
        offset: u64,
        data: Vec<u8>,
        done: bool,
        file_size: u64,
        modified_ts: i64,
        sha256: Option<String>,
    },
}

pub async fn write_message<W>(writer: &mut W, msg: &Message) -> Result<()>
where
    W: AsyncWrite + Unpin,
{
    let bytes = bincode::serialize(msg).context("serialize message failed")?;
    let len = bytes.len() as u32;
    writer
        .write_u32_le(len)
        .await
        .context("write message length failed")?;
    writer
        .write_all(&bytes)
        .await
        .context("write message body failed")?;
    writer.flush().await.context("flush failed")?;
    Ok(())
}

pub async fn read_message<R>(reader: &mut R) -> Result<Message>
where
    R: AsyncRead + Unpin,
{
    let len = reader.read_u32_le().await.context("read message length failed")?;
    if len > 256 * 1024 * 1024 {
        return Err(anyhow!("message too large"));
    }

    let mut buf = vec![0u8; len as usize];
    reader
        .read_exact(&mut buf)
        .await
        .context("read message body failed")?;
    let msg: Message = bincode::deserialize(&buf).context("deserialize message failed")?;
    Ok(msg)
}
