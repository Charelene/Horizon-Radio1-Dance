//! Small shared helpers: error type, prompts, progress output, PATH lookup.
use std::io::{self, IsTerminal, Write};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error + Send + Sync>>;

pub fn fail<T>(msg: impl Into<String>) -> Result<T> {
    Err(msg.into().into())
}

static ASSUME_YES: AtomicBool = AtomicBool::new(false);

pub enum Msg {
    Log(String),
    /// a one-line readout that keeps changing (download progress)
    Status(String),
}

pub fn emit(m: Msg) {
    match m {
        Msg::Log(s) => println!("{s}"),
        Msg::Status(s) if io::stdout().is_terminal() => {
            print!("\r  {s:<70}");
            io::stdout().flush().ok();
        }
        Msg::Status(_) => {}
    }
}

#[macro_export]
macro_rules! say {
    ($($a:tt)*) => { $crate::util::emit($crate::util::Msg::Log(format!($($a)*))) };
}

pub fn assume_yes(on: bool) {
    ASSUME_YES.store(on, Ordering::Relaxed);
}

pub fn ask_yes_no(question: &str, default: bool) -> bool {
    if ASSUME_YES.load(Ordering::Relaxed) || !io::stdin().is_terminal() {
        return default;
    }
    loop {
        print!("{question} [{}] ", if default { "Y/n" } else { "y/N" });
        io::stdout().flush().ok();
        let mut line = String::new();
        if io::stdin().read_line(&mut line).unwrap_or(0) == 0 {
            return default;
        }
        match line.trim().to_ascii_lowercase().as_str() {
            "" => return default,
            "y" | "yes" => return true,
            "n" | "no" => return false,
            _ => println!("  please answer y or n"),
        }
    }
}

pub fn ask_line(question: &str) -> Option<String> {
    if !io::stdin().is_terminal() {
        return None;
    }
    print!("{question} ");
    io::stdout().flush().ok();
    let mut line = String::new();
    io::stdin().read_line(&mut line).ok()?;
    let s = line.trim().trim_matches('"').to_string();
    (!s.is_empty()).then_some(s)
}

/// A rate-limited one-line readout for things that update constantly (downloads).
pub struct Line {
    last: std::time::Instant,
}

impl Line {
    pub fn new() -> Self {
        Line { last: std::time::Instant::now() - std::time::Duration::from_secs(1) }
    }
    pub fn update(&mut self, text: &str) {
        if self.last.elapsed().as_millis() > 120 {
            emit(Msg::Status(text.to_string()));
            self.last = std::time::Instant::now();
        }
    }
    pub fn done(&mut self, text: &str) {
        if io::stdout().is_terminal() {
            print!("\r");
        }
        emit(Msg::Status(String::new()));
        say!("  {text:<70}");
    }
}

pub fn wait_for_enter() {
    print!("\nPress Enter to close.");
    io::stdout().flush().ok();
    io::stdin().read_line(&mut String::new()).ok();
}

pub fn mb(bytes: u64) -> String {
    format!("{:.1} MB", bytes as f64 / 1e6)
}

pub fn remove_dir_if_exists(p: &Path) -> Result<()> {
    if p.exists() {
        std::fs::remove_dir_all(p).map_err(|e| format!("cannot remove {}: {e}", p.display()))?;
    }
    Ok(())
}
