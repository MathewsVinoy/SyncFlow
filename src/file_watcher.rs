use anyhow::{Context, Result};
use notify::event::{CreateKind, ModifyKind, RemoveKind, RenameMode};
use notify::{Config, Event, EventKind, RecommendedWatcher, RecursiveMode, Watcher};
use std::path::PathBuf;
use tokio::sync::mpsc;

use crate::sync_engine::LocalChange;
use crate::utils;

pub fn start(
    root: PathBuf,
    out_tx: mpsc::UnboundedSender<LocalChange>,
) -> Result<RecommendedWatcher> {
    let (raw_tx, raw_rx) = std::sync::mpsc::channel::<notify::Result<Event>>();

    let mut watcher = RecommendedWatcher::new(
        move |res| {
            let _ = raw_tx.send(res);
        },
        Config::default(),
    )
    .context("failed to create filesystem watcher")?;

    watcher
        .watch(&root, RecursiveMode::Recursive)
        .with_context(|| format!("failed to watch {}", root.display()))?;

    std::thread::spawn(move || {
        while let Ok(event) = raw_rx.recv() {
            let Ok(event) = event else {
                continue;
            };
            handle_event(&root, event, &out_tx);
        }
    });

    Ok(watcher)
}

fn handle_event(root: &PathBuf, event: Event, out_tx: &mpsc::UnboundedSender<LocalChange>) {
    let emit_change = |path: &std::path::Path, out_tx: &mpsc::UnboundedSender<LocalChange>| {
        if let Ok(rel) = path.strip_prefix(root)
            && let Ok(rel_str) = utils::normalize_relative_path(rel)
        {
            if rel_str.ends_with(".syncflow.part") {
                return;
            }
            let _ = out_tx.send(LocalChange::Changed(rel_str));
        }
    };

    let emit_delete = |path: &std::path::Path, out_tx: &mpsc::UnboundedSender<LocalChange>| {
        if let Ok(rel) = path.strip_prefix(root)
            && let Ok(rel_str) = utils::normalize_relative_path(rel)
        {
            if rel_str.ends_with(".syncflow.part") {
                return;
            }
            let _ = out_tx.send(LocalChange::Deleted(rel_str));
        }
    };

    match event.kind {
        EventKind::Create(CreateKind::File)
        | EventKind::Modify(ModifyKind::Data(_))
        | EventKind::Modify(ModifyKind::Metadata(_))
        | EventKind::Modify(ModifyKind::Any)
        | EventKind::Modify(ModifyKind::Name(RenameMode::To)) => {
            for p in &event.paths {
                emit_change(p, out_tx);
            }
        }
        EventKind::Remove(RemoveKind::File)
        | EventKind::Modify(ModifyKind::Name(RenameMode::From)) => {
            for p in &event.paths {
                emit_delete(p, out_tx);
            }
        }
        _ => {}
    }
}
