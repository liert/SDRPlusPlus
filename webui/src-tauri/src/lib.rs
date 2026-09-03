mod shm;

use std::fs::OpenOptions;
use std::io::Read;
use std::process::{Child, Command};
use std::sync::Mutex;
use std::time::Duration;
use tauri::WindowEvent;
use shm::{ShmManager, ShmStatusInfo, GLOBAL_SHM, log_to_file};

static BACKEND_CHILD: Mutex<Option<Child>> = Mutex::new(None);

fn is_backend_shm_active() -> bool {
    let mut lock = GLOBAL_SHM.lock().unwrap();
    if lock.is_none() {
        *lock = Some(ShmManager::new());
    }
    if let Some(mgr) = lock.as_mut() {
        mgr.try_connect()
    } else {
        false
    }
}

fn spawn_backend() -> bool {
    if is_backend_shm_active() {
        log_to_file("INFO", "Tauri Launcher", "C++ Backend is already running and connected via Shared Memory.");
        return true;
    }

    if let Ok(lock) = BACKEND_CHILD.lock() {
        if lock.is_some() {
            return true;
        }
    }

    if let Ok(current_exe) = std::env::current_exe() {
        if let Some(exe_dir) = current_exe.parent() {
            let candidates = [
                exe_dir.join("sdrpp.exe"),
                exe_dir.join("../../sdrpp_bin/sdrpp.exe"),
                exe_dir.join("../../../sdrpp_bin/sdrpp.exe"),
                std::path::PathBuf::from("sdrpp_bin/sdrpp.exe"),
            ];

            let mut target_backend = None;
            for c in &candidates {
                if c.exists() {
                    target_backend = Some(c.clone());
                    break;
                }
            }

            if let Some(backend_path) = target_backend {
                let run_dir = backend_path.parent().unwrap_or(exe_dir);
                let log_path = run_dir.join("sdrpp_backend.log");

                let mut cmd = Command::new(&backend_path);
                cmd.args(["-s", "-r", ".", "-c", "--from-gui"])
                   .current_dir(run_dir);

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
                    log_to_file("INFO", "Tauri Launcher", &format!("Spawned C++ Backend process: {:?} in {:?}", backend_path, run_dir));
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
fn get_shm_fft() -> Vec<f32> {
    let mut lock = GLOBAL_SHM.lock().unwrap();
    if lock.is_none() {
        *lock = Some(ShmManager::new());
    }
    if let Some(mgr) = lock.as_mut() {
        if let Some(vec) = mgr.read_fft_f32() {
            return vec;
        }
    }
    Vec::new()
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
            devices: Vec::new(),
            packets: Vec::new(),
            fft_size: 1024,
            fft_window: 0,
            fft_rate: 60,
            file_loaded: false,
            current_file: String::new(),
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
fn log_frontend_message(level: String, msg: String) {
    log_to_file(&level.to_uppercase(), "WebUI / Canvas", &msg);
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

#[repr(C)]
struct OpenFileNameW {
    l_struct_size: u32,
    hwnd_owner: usize,
    h_instance: usize,
    lpstr_filter: *const u16,
    lpstr_custom_filter: *mut u16,
    n_max_cust_filter: u32,
    n_filter_index: u32,
    lpstr_file: *mut u16,
    n_max_file: u32,
    lpstr_file_title: *mut u16,
    n_max_file_title: u32,
    lpstr_initial_dir: *const u16,
    lpstr_title: *const u16,
    flags: u32,
    n_file_offset: u16,
    n_file_extension: u16,
    lpstr_def_ext: *const u16,
    l_cust_data: usize,
    lpfn_hook: usize,
    lp_template_name: *const u16,
    pv_reserved: usize,
    dw_reserved: u32,
    flags_ex: u32,
}

#[link(name = "comdlg32")]
extern "system" {
    fn GetOpenFileNameW(lpofn: *mut OpenFileNameW) -> i32;
}

#[tauri::command]
fn open_iq_file_dialog() -> Option<String> {
    let mut file_buf = [0u16; 1024];
    let filter: Vec<u16> = "IQ / WAV Files (*.iq;*.raw;*.wav;*.bin)\0*.iq;*.raw;*.wav;*.bin\0All Files (*.*)\0*.*\0\0"
        .encode_utf16()
        .collect();
    let title: Vec<u16> = "选择 IQ 射频原始数据文件\0".encode_utf16().collect();

    let mut ofn: OpenFileNameW = unsafe { std::mem::zeroed() };
    ofn.l_struct_size = std::mem::size_of::<OpenFileNameW>() as u32;
    ofn.lpstr_filter = filter.as_ptr();
    ofn.lpstr_file = file_buf.as_mut_ptr();
    ofn.n_max_file = file_buf.len() as u32;
    ofn.lpstr_title = title.as_ptr();
    ofn.flags = 0x00000800 | 0x00001000; // OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST

    if unsafe { GetOpenFileNameW(&mut ofn) } != 0 {
        let len = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
        let selected = String::from_utf16(&file_buf[..len]).ok();
        if let Some(ref p) = selected {
            log_to_file("INFO", "Tauri Native Dialog", &format!("User selected file: {}", p));
        }
        selected
    } else {
        None
    }
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
            log_frontend_message,
            get_backend_logs,
            restart_backend,
            open_iq_file_dialog
        ])
        .setup(|_app| {
            // Auto-launch C++ backend on startup
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
