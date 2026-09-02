mod shm;

use std::net::TcpStream;
use std::process::{Child, Command};
use std::sync::Mutex;
use std::time::Duration;
use tauri::WindowEvent;
use shm::{ShmManager, ShmStatusInfo, GLOBAL_SHM};

static BACKEND_CHILD: Mutex<Option<Child>> = Mutex::new(None);

fn is_server_running() -> bool {
    if let Ok(addr) = "127.0.0.1:5259".parse() {
        TcpStream::connect_timeout(&addr, Duration::from_millis(300)).is_ok()
    } else {
        false
    }
}

#[tauri::command]
fn get_shm_fft() -> tauri::ipc::Response {
    let mut lock = GLOBAL_SHM.lock().unwrap();
    if lock.is_none() {
        *lock = Some(ShmManager::new());
    }
    if let Some(mgr) = lock.as_mut() {
        if let Some(bytes) = mgr.read_fft_raw() {
            return tauri::ipc::Response::new(bytes);
        }
    }
    tauri::ipc::Response::new(Vec::new())
}

#[tauri::command]
fn get_shm_status() -> ShmStatusInfo {
    let mut lock = GLOBAL_SHM.lock().unwrap();
    if lock.is_none() {
        *lock = Some(ShmManager::new());
    }
    if let Some(mgr) = lock.as_mut() {
        mgr.read_status()
    } else {
        ShmStatusInfo {
            connected: false,
            seq: 0,
            running: false,
            sample_rate: 8000000.0,
            center_freq: 2400000000.0,
            lna_gain: 32,
            vga_gain: 20,
            amp_enable: false,
            bias_t: false,
            source: "HackRF".to_string(),
            device_serial: "".to_string(),
            packets: Vec::new(),
        }
    }
}

#[tauri::command]
fn send_shm_cmd(cmd: String, params: serde_json::Value) -> bool {
    let mut lock = GLOBAL_SHM.lock().unwrap();
    if lock.is_none() {
        *lock = Some(ShmManager::new());
    }
    if let Some(mgr) = lock.as_mut() {
        mgr.send_command(&cmd, &params)
    } else {
        false
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_shm_fft,
            get_shm_status,
            send_shm_cmd
        ])
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
