//! Aether: Hyprland's real dwindle - a split tree per workspace.
//!
//! komorebi's BSP (and Aether's chain dwindle on top of it) places windows by list order only: window i takes its
//! share of what is left. A new window could therefore only be put before or after another in that list, never into
//! the hovered window's top-left (the user, 2026-11-22). Here every new window splits the tile under the mouse, on
//! the side the mouse is on, and a closed window gives its space back to its sibling, like Hyprland.
//!
//! Trees are keyed by their containers' ids (nanoids, unique), so no Workspace field is needed. A workspace with no
//! tree yet (start-up, other layouts) keeps the chain dwindle; its first mouse-placed window turns the current chain
//! into a tree. Swapping/moving windows (which reorders the container list) moves the windows between the tiles.

use crate::core::Rect;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::LazyLock;

#[derive(Clone, Debug)]
enum Node {
    Leaf(String),
    Split {
        /// Some(true) = side by side, Some(false) = top / bottom, None = by the shape of the space (like Hyprland)
        side: Option<bool>,
        /// togglesplit on an automatic split
        flip: bool,
        /// share of the space given to `a`; 0 = the default ratio
        ratio: f32,
        a: Box<Node>,
        b: Box<Node>,
    },
}

struct Tree {
    root: Node,
    /// the container order last laid out (to notice swaps)
    order: Vec<String>,
}

static TREES: LazyLock<Mutex<Vec<Tree>>> = LazyLock::new(|| Mutex::new(Vec::new()));

#[derive(Clone, Copy)]
pub struct Params {
    pub multiplier: f32,
    pub default_ratio: f32,
}

impl Params {
    pub fn from(o: &komorebi_layouts::DwindleLayoutOptions) -> Self {
        Self { multiplier: o.split_width_multiplier.unwrap_or(1.0), default_ratio: o.default_ratio.unwrap_or(0.5) }
    }
}

impl Node {
    fn leaves(&self, out: &mut Vec<String>) {
        match self {
            Node::Leaf(id) => out.push(id.clone()),
            Node::Split { a, b, .. } => {
                a.leaves(out);
                b.leaves(out);
            }
        }
    }

    /// this node without the leaf `id` (None when nothing is left)
    fn without(self, id: &str) -> Option<Node> {
        match self {
            Node::Leaf(l) => (l != id).then_some(Node::Leaf(l)),
            Node::Split { side, flip, ratio, a, b } => match (a.without(id), b.without(id)) {
                (Some(a), Some(b)) => Some(Node::Split { side, flip, ratio, a: Box::new(a), b: Box::new(b) }),
                (Some(n), None) | (None, Some(n)) => Some(n),
                (None, None) => None,
            },
        }
    }

    fn relabel(&mut self, map: &HashMap<String, String>) {
        match self {
            Node::Leaf(id) => {
                if let Some(n) = map.get(id) {
                    *id = n.clone();
                }
            }
            Node::Split { a, b, .. } => {
                a.relabel(map);
                b.relabel(map);
            }
        }
    }

    /// replace the leaf `id` with `f(leaf)`; true when found
    fn replace_leaf(&mut self, id: &str, f: &mut dyn FnMut(Node) -> Node) -> bool {
        match self {
            Node::Leaf(l) if l == id => {
                let old = std::mem::replace(self, Node::Leaf(String::new()));
                *self = f(old);
                true
            }
            Node::Leaf(_) => false,
            Node::Split { a, b, .. } => a.replace_leaf(id, f) || b.replace_leaf(id, f),
        }
    }

    fn rects(&self, area: Rect, p: Params, out: &mut HashMap<String, Rect>) {
        match self {
            Node::Leaf(id) => {
                out.insert(id.clone(), area);
            }
            Node::Split { a, b, .. } => {
                let (ra, rb) = split_area(self, area, p);
                a.rects(ra, p, out);
                b.rects(rb, p, out);
            }
        }
    }
}

fn effective_side(node: &Node, area: Rect, p: Params) -> bool {
    match node {
        Node::Split { side: Some(s), .. } => *s,
        Node::Split { side: None, flip, .. } => (area.right as f32 > area.bottom as f32 * p.multiplier) ^ *flip,
        Node::Leaf(_) => true,
    }
}

fn split_area(node: &Node, area: Rect, p: Params) -> (Rect, Rect) {
    let ratio = match node {
        Node::Split { ratio, .. } if *ratio > 0.0 => *ratio,
        _ => p.default_ratio,
    }
    .clamp(0.05, 0.95);
    if effective_side(node, area, p) {
        let w = (area.right as f32 * ratio).round() as i32;
        (
            Rect { left: area.left, top: area.top, right: w, bottom: area.bottom },
            Rect { left: area.left + w, top: area.top, right: area.right - w, bottom: area.bottom },
        )
    } else {
        let h = (area.bottom as f32 * ratio).round() as i32;
        (
            Rect { left: area.left, top: area.top, right: area.right, bottom: h },
            Rect { left: area.left, top: area.top + h, right: area.right, bottom: area.bottom - h },
        )
    }
}

/// the chain dwindle (window i takes its share of the rest) as a tree, keeping its flips and ratios
fn chain(ids: &[String], flips: u32, ratios: &[f32]) -> Option<Node> {
    let mut node = Node::Leaf(ids.last()?.clone());
    for i in (0..ids.len() - 1).rev() {
        node = Node::Split {
            side: None,
            flip: i < 32 && (flips >> i) & 1 == 1,
            ratio: ratios.get(i).copied().unwrap_or(0.0),
            a: Box::new(Node::Leaf(ids[i].clone())),
            b: Box::new(node),
        };
    }
    Some(node)
}

fn overlap(tree: &Tree, ids: &[String]) -> usize {
    let mut leaves = Vec::new();
    tree.root.leaves(&mut leaves);
    leaves.iter().filter(|l| ids.contains(l)).count()
}

/// take the tree that shares the most containers with `ids` out of the store
fn take(trees: &mut Vec<Tree>, ids: &[String]) -> Option<Tree> {
    let (i, n) = trees.iter().enumerate().map(|(i, t)| (i, overlap(t, ids))).max_by_key(|(_, n)| *n)?;
    (n > 0).then(|| trees.remove(i))
}

fn put(trees: &mut Vec<Tree>, tree: Tree) {
    trees.push(tree);
    if trees.len() > 64 {
        trees.remove(0);
    }
}

/// bring the tree in line with the workspace's containers: closed ones give their space to their sibling, a swap in
/// the list swaps the tiles, containers it doesn't know split the tile of the one before them
fn reconcile(mut tree: Tree, ids: &[String], area: Rect, p: Params) -> Option<Tree> {
    let mut leaves = Vec::new();
    tree.root.leaves(&mut leaves);
    let mut root = Some(tree.root);
    for l in leaves.iter().filter(|l| !ids.contains(l)) {
        root = root.and_then(|r| r.without(l));
    }
    let mut root = root?;

    // positions: the known containers in the old order vs. the new order
    let old: Vec<&String> = tree.order.iter().filter(|id| ids.contains(id) && leaves.contains(id)).collect();
    let new: Vec<&String> = ids.iter().filter(|id| leaves.contains(id)).collect();
    if old.len() == new.len() && old != new {
        let map: HashMap<String, String> =
            old.iter().zip(new.iter()).filter(|(o, n)| o != n).map(|(o, n)| ((*o).clone(), (*n).clone())).collect();
        root.relabel(&map);
    }

    for (k, id) in ids.iter().enumerate() {
        if leaves.contains(id) {
            continue;
        }
        let mut rects = HashMap::new();
        root.rects(area, p, &mut rects);
        let anchor = ids[..k].iter().rev().find(|a| rects.contains_key(*a)).cloned().or_else(|| {
            let mut all = Vec::new();
            root.leaves(&mut all);
            all.last().cloned()
        })?;
        let r = rects[&anchor];
        let side = r.right as f32 > r.bottom as f32 * p.multiplier;
        let new_id = id.clone();
        root.replace_leaf(&anchor, &mut |old| Node::Split {
            side: Some(side),
            flip: false,
            ratio: 0.0,
            a: Box::new(old),
            b: Box::new(Node::Leaf(new_id.clone())),
        });
        leaves.push(id.clone());
    }
    tree.root = root;
    tree.order = ids.to_vec();
    Some(tree)
}

/// the tiles of `ids` (in that order) when the workspace has a tree, else None (the chain dwindle is used)
pub fn layout(ids: &[String], area: &Rect, padding: i32, p: Params) -> Option<Vec<Rect>> {
    let mut trees = TREES.lock();
    let tree = reconcile(take(&mut trees, ids)?, ids, *area, p)?;
    let mut rects = HashMap::new();
    tree.root.rects(*area, p, &mut rects);
    let out = ids
        .iter()
        .map(|id| {
            let mut r = rects.get(id).copied().unwrap_or(*area);
            r.add_padding(padding);
            r
        })
        .collect();
    put(&mut trees, tree);
    Some(out)
}

/// a new container `new_id` splits `target`'s tile: side by side or stacked, the new one first (left / top) or second.
/// `before` = the containers before the insert (for a workspace without a tree yet: its chain), `after` = after it.
pub fn insert(
    before: &[String],
    after: &[String],
    target: &str,
    new_id: &str,
    side_by_side: bool,
    new_first: bool,
    flips: u32,
    ratios: &[f32],
) {
    let mut trees = TREES.lock();
    let mut tree = match take(&mut trees, before) {
        Some(t) => t,
        None => match chain(before, flips, ratios) {
            Some(root) => Tree { root, order: before.to_vec() },
            None => return,
        },
    };
    let new_leaf = Node::Leaf(new_id.to_string());
    let mut new_leaf = Some(new_leaf);
    let found = tree.root.replace_leaf(target, &mut |old| {
        let n = new_leaf.take().unwrap_or(Node::Leaf(String::new()));
        let (a, b) = if new_first { (n, old) } else { (old, n) };
        Node::Split { side: Some(side_by_side), flip: false, ratio: 0.0, a: Box::new(a), b: Box::new(b) }
    });
    if found {
        tree.order = after.to_vec();
    }
    put(&mut trees, tree);
}

/// togglesplit / resizeactive on a workspace with a tree; false when it has none (the chain dwindle handles it)
pub fn action(ids: &[String], focused: &str, area: &Rect, p: Params, dispatcher: &str, dx: f32, dy: f32) -> bool {
    let mut trees = TREES.lock();
    let Some(tree) = take(&mut trees, ids) else { return false };
    let Some(mut tree) = reconcile(tree, ids, *area, p) else { return false };
    match dispatcher {
        "togglesplit" => {
            toggle(&mut tree.root, focused, *area, p);
        }
        _ => {
            if dx != 0.0 {
                resize(&mut tree.root, focused, *area, p, true, dx);
            }
            if dy != 0.0 {
                resize(&mut tree.root, focused, *area, p, false, dy);
            }
        }
    }
    put(&mut trees, tree);
    true
}

fn contains(node: &Node, id: &str) -> bool {
    match node {
        Node::Leaf(l) => l == id,
        Node::Split { a, b, .. } => contains(a, id) || contains(b, id),
    }
}

/// flip the split right above the focused window
fn toggle(node: &mut Node, id: &str, area: Rect, p: Params) -> bool {
    let eff = effective_side(node, area, p);
    let (ra, rb) = split_area(node, area, p);
    let Node::Split { side, flip, a, b, .. } = node else { return false };
    let a_is = matches!(a.as_ref(), Node::Leaf(l) if l == id);
    let b_is = matches!(b.as_ref(), Node::Leaf(l) if l == id);
    if a_is || b_is {
        match side {
            Some(s) => *s = !*s,
            None => *flip = !*flip,
        }
        let _ = eff;
        return true;
    }
    toggle(a, id, ra, p) || toggle(b, id, rb, p)
}

/// move the nearest split above the focused window that runs this way by `delta` pixels (the window grows)
/// returns: 0 = not in this subtree, 1 = found, still looking for a split, 2 = done
fn resize(node: &mut Node, id: &str, area: Rect, p: Params, side_by_side: bool, delta: f32) -> u8 {
    if let Node::Leaf(l) = node {
        return u8::from(l == id);
    }
    let eff = effective_side(node, area, p);
    let (ra, rb) = split_area(node, area, p);
    let default_ratio = p.default_ratio;
    let Node::Split { ratio, a, b, .. } = node else { return 0 };
    let (r, in_a) = if contains(a, id) { (resize(a, id, ra, p, side_by_side, delta), true) } else { (resize(b, id, rb, p, side_by_side, delta), false) };
    if r != 1 {
        return r;
    }
    if eff != side_by_side {
        return 1;
    }
    let size = if side_by_side { area.right } else { area.bottom } as f32;
    if size <= 0.0 {
        return 2;
    }
    let current = if *ratio > 0.0 { *ratio } else { default_ratio };
    let sign = if in_a { 1.0 } else { -1.0 };
    *ratio = (current + sign * delta / size).clamp(0.05, 0.95);
    2
}
