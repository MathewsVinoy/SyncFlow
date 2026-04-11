use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::sync::Arc;
use tokio::net::UdpSocket;
use tokio::sync::{mpsc, Mutex};
use tokio::time::{interval, Duration};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct DiscoveryPacket {
    node_id: String,
    device_name: String,
    token: String,
    tcp_port: u16,
}

#[derive(Clone)]
pub struct DiscoveryConfig {
    pub node_id: String,
    pub device_name: String,
    pub token: String,
    pub tcp_port: u16,
    pub udp_port: u16,
}

pub async fn start(
    cfg: DiscoveryConfig,
    discovered_tx: mpsc::UnboundedSender<SocketAddr>,
) -> Result<()> {
    let seen = Arc::new(Mutex::new(HashSet::<SocketAddr>::new()));

    let recv_socket = UdpSocket::bind(SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), cfg.udp_port))
        .await
        .with_context(|| format!("failed to bind discovery receiver on UDP {}", cfg.udp_port))?;

    let send_socket = UdpSocket::bind(SocketAddr::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED), 0))
        .await
        .context("failed to bind discovery sender")?;
    send_socket
        .set_broadcast(true)
        .context("failed to enable UDP broadcast")?;

    let send_cfg = cfg.clone();
    tokio::spawn(async move {
        let mut tick = interval(Duration::from_secs(2));
        loop {
            tick.tick().await;
            let pkt = DiscoveryPacket {
                node_id: send_cfg.node_id.clone(),
                device_name: send_cfg.device_name.clone(),
                token: send_cfg.token.clone(),
                tcp_port: send_cfg.tcp_port,
            };
            if let Ok(bytes) = bincode::serialize(&pkt) {
                let target = SocketAddr::new(IpAddr::V4(Ipv4Addr::BROADCAST), send_cfg.udp_port);
                let _ = send_socket.send_to(&bytes, target).await;
            }
        }
    });

    let recv_cfg = cfg.clone();
    tokio::spawn(async move {
        let mut buf = vec![0u8; 2048];
        loop {
            let Ok((n, from)) = recv_socket.recv_from(&mut buf).await else {
                continue;
            };

            let Ok(pkt) = bincode::deserialize::<DiscoveryPacket>(&buf[..n]) else {
                continue;
            };

            if pkt.node_id == recv_cfg.node_id || pkt.token != recv_cfg.token {
                continue;
            }

            let peer = SocketAddr::new(from.ip(), pkt.tcp_port);
            let mut seen_lock = seen.lock().await;
            if seen_lock.insert(peer) {
                let _ = discovered_tx.send(peer);
            }
        }
    });

    Ok(())
}
