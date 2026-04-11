mod discovery;
mod file_watcher;
mod network;
mod sync_engine;
mod transfer;
mod utils;

use anyhow::{Context, Result};
use clap::Parser;
use std::collections::HashSet;
use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{mpsc, Mutex, RwLock};

use discovery::DiscoveryConfig;
use network::{Hello, Message};
use sync_engine::SyncEngine;

#[derive(Debug, Parser)]
#[command(name = "syncflow", version, about = "P2P LAN folder sync")]
struct Cli {
    #[arg(long)]
    dir: PathBuf,

    #[arg(long, default_value = "0.0.0.0:7878")]
    listen: SocketAddr,

    #[arg(long = "peer")]
    peers: Vec<SocketAddr>,

    #[arg(long, default_value = "device")]
    device_name: String,

    #[arg(long, default_value = "syncflow-token")]
    token: String,

    #[arg(long, default_value_t = true)]
    watch: bool,

    #[arg(long, default_value_t = true)]
    discover: bool,

    #[arg(long, default_value_t = 9999)]
    discovery_port: u16,
}

#[derive(Clone)]
struct PeerHub {
    senders: Arc<RwLock<Vec<mpsc::UnboundedSender<Message>>>>,
}

impl PeerHub {
    fn new() -> Self {
        Self {
            senders: Arc::new(RwLock::new(Vec::new())),
        }
    }

    async fn add(&self, tx: mpsc::UnboundedSender<Message>) {
        self.senders.write().await.push(tx);
    }

    async fn broadcast(&self, msg: Message) {
        let mut guard = self.senders.write().await;
        guard.retain(|tx| tx.send(msg.clone()).is_ok());
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();

    tokio::fs::create_dir_all(&cli.dir)
        .await
        .with_context(|| format!("failed to create sync directory: {}", cli.dir.display()))?;
    let root = tokio::fs::canonicalize(&cli.dir)
        .await
        .with_context(|| format!("failed to canonicalize: {}", cli.dir.display()))?;

    let engine = SyncEngine::new(root.clone());
    let hub = PeerHub::new();

    let node_id = uuid::Uuid::new_v4().to_string();

    let connected = Arc::new(Mutex::new(HashSet::<SocketAddr>::new()));

    run_listener(
        cli.listen,
        engine.clone(),
        hub.clone(),
        cli.device_name.clone(),
        cli.token.clone(),
        node_id.clone(),
        connected.clone(),
    )
    .await?;

    for peer in cli.peers {
        let _ = connect_to_peer(
            peer,
            engine.clone(),
            hub.clone(),
            cli.device_name.clone(),
            cli.token.clone(),
            node_id.clone(),
            connected.clone(),
        )
        .await;
    }

    if cli.discover {
        let (disc_tx, mut disc_rx) = mpsc::unbounded_channel::<SocketAddr>();
        discovery::start(
            DiscoveryConfig {
                node_id: node_id.clone(),
                device_name: cli.device_name.clone(),
                token: cli.token.clone(),
                tcp_port: cli.listen.port(),
                udp_port: cli.discovery_port,
            },
            disc_tx,
        )
        .await?;

        let engine_clone = engine.clone();
        let hub_clone = hub.clone();
        let device_name = cli.device_name.clone();
        let token = cli.token.clone();
        let node_id_clone = node_id.clone();
        let connected_clone = connected.clone();

        tokio::spawn(async move {
            while let Some(peer) = disc_rx.recv().await {
                let _ = connect_to_peer(
                    peer,
                    engine_clone.clone(),
                    hub_clone.clone(),
                    device_name.clone(),
                    token.clone(),
                    node_id_clone.clone(),
                    connected_clone.clone(),
                )
                .await;
            }
        });
    }

    if cli.watch {
        let (watch_tx, mut watch_rx) = mpsc::unbounded_channel();
        let _watcher = file_watcher::start(root, watch_tx)?;

        while let Some(change) = watch_rx.recv().await {
            if let Ok(Some(msg)) = engine.local_change_to_message(change).await {
                hub.broadcast(msg).await;
            }
        }
    } else {
        tokio::signal::ctrl_c().await.context("ctrl_c wait failed")?;
    }

    Ok(())
}

async fn run_listener(
    listen: SocketAddr,
    engine: SyncEngine,
    hub: PeerHub,
    device_name: String,
    token: String,
    node_id: String,
    connected: Arc<Mutex<HashSet<SocketAddr>>>,
) -> Result<()> {
    let listener = TcpListener::bind(listen)
        .await
        .with_context(|| format!("failed to listen on {listen}"))?;

    tokio::spawn(async move {
        loop {
            let Ok((stream, peer)) = listener.accept().await else {
                continue;
            };

            let engine = engine.clone();
            let hub = hub.clone();
            let device_name = device_name.clone();
            let token = token.clone();
            let node_id = node_id.clone();
            let connected = connected.clone();

            tokio::spawn(async move {
                let _ = handle_peer(
                    stream, peer, engine, hub, device_name, token, node_id, connected,
                )
                .await;
            });
        }
    });

    Ok(())
}

async fn connect_to_peer(
    peer: SocketAddr,
    engine: SyncEngine,
    hub: PeerHub,
    device_name: String,
    token: String,
    node_id: String,
    connected: Arc<Mutex<HashSet<SocketAddr>>>,
) -> Result<()> {
    {
        let guard = connected.lock().await;
        if guard.contains(&peer) {
            return Ok(());
        }
    }

    let stream = TcpStream::connect(peer)
        .await
        .with_context(|| format!("connect failed to {peer}"))?;

    handle_peer(stream, peer, engine, hub, device_name, token, node_id, connected).await
}

async fn handle_peer(
    stream: TcpStream,
    peer: SocketAddr,
    engine: SyncEngine,
    hub: PeerHub,
    device_name: String,
    token: String,
    node_id: String,
    connected: Arc<Mutex<HashSet<SocketAddr>>>,
) -> Result<()> {
    {
        let mut guard = connected.lock().await;
        guard.insert(peer);
    }

    let (mut reader, mut writer) = stream.into_split();
    let (out_tx, mut out_rx) = mpsc::unbounded_channel::<Message>();
    hub.add(out_tx.clone()).await;

    let write_task = tokio::spawn(async move {
        while let Some(msg) = out_rx.recv().await {
            if network::write_message(&mut writer, &msg).await.is_err() {
                break;
            }
        }
    });

    out_tx
        .send(Message::Hello(Hello {
            device_name,
            token: token.clone(),
            node_id,
        }))
        .map_err(|_| anyhow::anyhow!("failed to send hello"))?;

    let incoming_hello = network::read_message(&mut reader).await?;
    let remote_hello = match incoming_hello {
        Message::Hello(h) => h,
        _ => return Err(anyhow::anyhow!("expected hello message")),
    };
    if remote_hello.token != token {
        return Err(anyhow::anyhow!("authentication failed: token mismatch"));
    }

    out_tx
        .send(Message::ManifestRequest)
        .map_err(|_| anyhow::anyhow!("failed to send manifest request"))?;

    let my_manifest = engine.scan_manifest().await?;
    out_tx
        .send(Message::ManifestResponse { entries: my_manifest })
        .map_err(|_| anyhow::anyhow!("failed to send manifest response"))?;

    loop {
        let msg = match network::read_message(&mut reader).await {
            Ok(m) => m,
            Err(_) => break,
        };

        match msg {
            Message::Hello(_) => {}
            Message::ManifestRequest => {
                let entries = engine.scan_manifest().await?;
                let _ = out_tx.send(Message::ManifestResponse { entries });
            }
            Message::ManifestResponse { entries } => {
                let needs = engine.reconcile_manifest(&entries).await?;
                for m in needs {
                    let _ = out_tx.send(m);
                }
            }
            Message::FileChanged { entry } => {
                let needs = engine.reconcile_manifest(&[entry]).await?;
                for m in needs {
                    let _ = out_tx.send(m);
                }
            }
            Message::DeleteNotice { path } => {
                let _ = engine.apply_delete(&path).await;
            }
            Message::NeedFile { path, offset } => {
                let engine_clone = engine.clone();
                let tx_clone = out_tx.clone();
                tokio::spawn(async move {
                    let _ = transfer::send_file_chunks(&engine_clone, &path, offset, tx_clone).await;
                });
            }
            Message::FileChunk {
                path,
                offset,
                data,
                done,
                file_size,
                modified_ts,
                sha256,
            } => {
                let _ = engine
                    .handle_file_chunk(
                        &path,
                        offset,
                        &data,
                        done,
                        file_size,
                        modified_ts,
                        sha256.as_deref(),
                    )
                    .await;
            }
        }
    }

    write_task.abort();
    {
        let mut guard = connected.lock().await;
        guard.remove(&peer);
    }
    Ok(())
}
