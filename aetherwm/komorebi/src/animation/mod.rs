use crate::animation::animation_manager::AnimationManager;
use crate::core::animation::AnimationStyle;

use lazy_static::lazy_static;
use prefix::AnimationPrefix;
use std::collections::HashMap;
use std::sync::Arc;
use std::sync::atomic::AtomicBool;
use std::sync::atomic::AtomicU64;

use parking_lot::Mutex;

pub use engine::AnimationEngine;
pub mod animation_manager;
pub mod engine;
pub mod lerp;
pub mod prefix;
pub mod render_dispatcher;
pub use render_dispatcher::RenderDispatcher;
pub mod style;
pub mod workspace;

use serde::Deserialize;
use serde::Serialize;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[cfg_attr(feature = "schemars", derive(schemars::JsonSchema))]
#[serde(untagged)]
/// Animation configuration
///
/// This can be either global:
/// ```json
/// {
///     "enabled": true,
///     "style": "EaseInSine",
///     "fps": 60,
///     "duration": 250
/// }
/// ```
///
/// Or scoped by an animation kind prefix:
/// ```json
/// {
///     "movement": {
///         "enabled": true,
///         "style": "EaseInSine",
///         "fps": 60,
///         "duration": 250
///     }
/// }
/// ```
pub enum PerAnimationPrefixConfig<T> {
    /// Animation configuration prefixed for a specific animation kind
    Prefix(HashMap<AnimationPrefix, T>),
    /// Animation configuration for all animation kinds
    Global(T),
}

pub const DEFAULT_ANIMATION_ENABLED: bool = false;
pub const DEFAULT_ANIMATION_STYLE: AnimationStyle = AnimationStyle::Linear;
pub const DEFAULT_ANIMATION_DURATION: u64 = 250;
pub const DEFAULT_ANIMATION_FPS: u64 = 60;

lazy_static! {
    pub static ref ANIMATION_MANAGER: Arc<Mutex<AnimationManager>> =
        Arc::new(Mutex::new(AnimationManager::new()));
    pub static ref ANIMATION_STYLE_GLOBAL: Arc<Mutex<AnimationStyle>> =
        Arc::new(Mutex::new(DEFAULT_ANIMATION_STYLE));
    pub static ref ANIMATION_ENABLED_GLOBAL: Arc<AtomicBool> =
        Arc::new(AtomicBool::new(DEFAULT_ANIMATION_ENABLED));
    pub static ref ANIMATION_DURATION_GLOBAL: Arc<AtomicU64> =
        Arc::new(AtomicU64::new(DEFAULT_ANIMATION_DURATION));
    pub static ref ANIMATION_STYLE_PER_ANIMATION: Arc<Mutex<HashMap<AnimationPrefix, AnimationStyle>>> =
        Arc::new(Mutex::new(HashMap::new()));
    pub static ref ANIMATION_ENABLED_PER_ANIMATION: Arc<Mutex<HashMap<AnimationPrefix, bool>>> =
        Arc::new(Mutex::new(HashMap::new()));
    pub static ref ANIMATION_DURATION_PER_ANIMATION: Arc<Mutex<HashMap<AnimationPrefix, u64>>> =
        Arc::new(Mutex::new(HashMap::new()));
}

pub static ANIMATION_FPS: AtomicU64 = AtomicU64::new(DEFAULT_ANIMATION_FPS);

/// How long movement animations stay suppressed after a workspace switch
pub const WORKSPACE_SWITCH_ANIMATION_SUPPRESSION_MS: u64 = 400;

static ANIMATION_SUPPRESSED_UNTIL_MS: AtomicU64 = AtomicU64::new(0);

fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or_default()
}

/// Windows are repositioned instantly (without movement animations) for a short
/// while after a workspace switch, so that layout corrections which happen as the
/// workspace is restored don't show up as windows visibly shrinking and growing
pub fn suppress_movement_animations_after_workspace_switch() {
    ANIMATION_SUPPRESSED_UNTIL_MS.store(
        now_ms() + WORKSPACE_SWITCH_ANIMATION_SUPPRESSION_MS,
        std::sync::atomic::Ordering::SeqCst,
    );
}

pub fn movement_animations_suppressed() -> bool {
    now_ms() < ANIMATION_SUPPRESSED_UNTIL_MS.load(std::sync::atomic::Ordering::SeqCst)
}
