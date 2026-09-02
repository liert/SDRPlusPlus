use std::net::TcpStream;
use std::process::{Child, Command};
use std::sync::Mutex;
use std::time::Duration;
use tauri::WindowEvent;

static BACKEND_CHILD: Mutex<Option<Child>> = Mutex::new(None);

fn is_server_running() -> bool {
    if let Ok(addr) = "127.0.0.1:5259".parse() {
        TcpStream::connect_timeout(&addr, Duration::from_millis(300)).is_ok()
    } else {
        false
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|app| {
            if cfg!(debug_assertions) {
                app.handle().plugin(
                    tauri_plugin_log::Builder::default()
                        .level(log::LevelFilter::Info)
                        .build(),
                )?;
            }

            // Auto-launch C++ backend if not already running
            if !is_server_running() {
                if let Ok(current_exe) = std::env::current_exe() {
                    if let Some(exe_dir) = current_exe.parent() {
                        let backend_path = exe_dir.join("sdrpp.exe");
                        if backend_path.exists() {
                            let mut cmd = Command::new(&backend_path);
                            cmd.args(["-s", "-p", "5259", "-a", "0.0.0.0", "-r", ".", "-c"])
                               .current_dir(exe_dir);

                            #[cfg(windows)]
                            {
                                use std::os::windows::process::CommandExt;
                                const CREATE_NO_WINDOW: u32 = 0x08000000;
                                cmd.creation_flags(CREATE_NO_WINDOW);
                            }

                            if let Ok(child) = cmd.spawn() {
                                if let Ok(mut lock) = BACKEND_CHILD.lock() {
                                    *lock = Some(child);
                                }
                            }
                        }
                    }
                }
            }

            Ok(())
        })
        .on_window_event(|_window, event| {
            if let WindowEvent::Destroyed = event {
                if let Ok(mut lock) = BACKEND_CHILD.lock() {
                    if let Some(mut child) = lock.take() {
                        let _ = child.kill();
                    }
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
