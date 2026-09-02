mod shm;

use std::fs::OpenOptions;
use std::io::Read;
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

fn spawn_backend() -> bool {
    if is_server_running() {
        return true;
    }
    if let Ok(current_exe) = std::env::current_exe() {
        if let Some(exe_dir) = current_exe.parent() {
            let backend_path = exe_dir.join("sdrpp.exe");
            let log_path = exe_dir.join("sdrpp_backend.log");

            if backend_path.exists() {
                let mut cmd = Command::new(&backend_path);
                cmd.args(["-s", "-p", "5259", "-a", "0.0.0.0", "-r", ".", "-c"])
                   .current_dir(exe_dir);

                if let Ok(log_f) = OpenOptions::new().create(true).append(true).open(&log_path) {
                    if let Ok(f_out) = log_f.try_clone() {
                        cmd.stdout(f_out);
                    }
                    if let Ok(f_err) = log_f.try_clone() {
                        cmd.stderr(f_err);
                    }
                }

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
                    return true;
                }
            }
        }
    }
    false
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

#[tauri::command]
fn get_backend_logs() -> String {
    if let Ok(current_exe) = std::env::current_exe() {
        if let Some(exe_dir) = current_exe.parent() {
            let log_path = exe_dir.join("sdrpp_backend.log");
            if let Ok(mut f) = OpenOptions::new().read(true).open(&log_path) {
                let mut content = String::new();
                if f.read_to_string(&mut content).is_ok() {
                    let lines: Vec<&str> = content.lines().collect();
                    let start = if lines.len() > 100 { lines.len() - 100 } else { 0 };
                    return lines[start..].join("\n");
                }
            }
        }
    }
    "暂无 C++ 后端日志记录 (sdrpp_backend.log)。".to_string()
}

#[tauri::command]
fn restart_backend() -> bool {
    if let Ok(mut lock) = BACKEND_CHILD.lock() {
        if let Some(mut child) = lock.take() {
            let _ = child.kill();
        }
    }
    std::thread::sleep(Duration::from_millis(300));
    spawn_backend()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            get_shm_fft,
            get_shm_status,
            send_shm_cmd,
            get_backend_logs,
            restart_backend
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
            spawn_backend();

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
