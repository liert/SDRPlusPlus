use std::ffi::c_void;
use std::fs::OpenOptions;
use std::io::Write;
use std::sync::Mutex;
use serde::Serialize;
use serde_json::Value;

const SDRPP_SHM_NAME_W: &[u16] = &[
    b'L' as u16, b'o' as u16, b'c' as u16, b'a' as u16, b'l' as u16, b'\\' as u16,
    b'S' as u16, b'D' as u16, b'R' as u16, b'P' as u16, b'P' as u16, b'_' as u16,
    b'S' as u16, b'H' as u16, b'M' as u16, b'_' as u16,
    b'B' as u16, b'U' as u16, b'F' as u16, b'F' as u16, b'E' as u16, b'R' as u16,
    0
];

const SDRPP_SHM_CMD_NAME_W: &[u16] = &[
    b'L' as u16, b'o' as u16, b'c' as u16, b'a' as u16, b'l' as u16, b'\\' as u16,
    b'S' as u16, b'D' as u16, b'R' as u16, b'P' as u16, b'P' as u16, b'_' as u16,
    b'S' as u16, b'H' as u16, b'M' as u16, b'_' as u16,
    b'C' as u16, b'M' as u16, b'D' as u16,
    0
];

const FILE_MAP_ALL_ACCESS: u32 = 0xF001F;
const FILE_MAP_READ: u32 = 0x0004;

pub fn log_to_file(level: &str, tag: &str, msg: &str) {
    if let Ok(current_exe) = std::env::current_exe() {
        if let Some(exe_dir) = current_exe.parent() {
            let log_path = exe_dir.join("sdrpp_backend.log");
            if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(&log_path) {
                let now = std::time::SystemTime::now();
                if let Ok(dur) = now.duration_since(std::time::UNIX_EPOCH) {
                    let secs = dur.as_secs();
                    let ms = dur.subsec_millis();
                    let s = secs % 60;
                    let m = (secs / 60) % 60;
                    let h = (secs / 3600 + 8) % 24;
                    let _ = writeln!(f, "[{:02}:{:02}:{:02}.{:03}] [{}] [{}] {}", h, m, s, ms, level, tag, msg);
                }
            }
        }
    }
}

#[link(name = "kernel32")]
extern "system" {
    fn OpenFileMappingW(desired_access: u32, inherit_handle: i32, name: *const u16) -> isize;
    fn MapViewOfFile(
        file_mapping_object: isize,
        desired_access: u32,
        file_offset_high: u32,
        file_offset_low: u32,
        number_of_bytes_to_map: usize,
    ) -> *mut c_void;
    fn CloseHandle(handle: isize) -> i32;
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ShmPacket {
    pub id: u32,
    pub sync_word: u32,
    pub mask: u8,
    pub crc_valid: u8,
    pub payload_len: u16,
    pub hw_crc: u32,
    pub freq_offset_khz: f32,
    pub timestamp: [u8; 32],
    pub payload: [u8; 128],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ShmDeviceInfo {
    pub serial: [u8; 64],
    pub name: [u8; 64],
    pub index: i32,
}

#[repr(C)]
pub struct ShmHeader {
    pub magic: u32,          // 0x53445250
    pub version: u32,        // 1
    pub seq: u32,            // Atomic increment per FFT frame
    pub fft_size: u32,       // 1024
    pub sample_rate: f64,    // 8000000.0
    pub center_freq: f64,    // 2400000000.0
    pub lna_gain: i32,
    pub vga_gain: i32,
    pub amp_enable: u8,
    pub bias_t_enable: u8,
    pub running: u8,
    pub source_id: u8,
    pub source_name: [u8; 32],
    pub device_serial: [u8; 64],
    
    // Connected Hardware Devices
    pub device_count: u32,
    pub devices: [ShmDeviceInfo; 8],

    // Decoded FLRC Packet Ring Buffer
    pub packet_seq: u32,
    pub packet_write_idx: u32,
    pub packets: [ShmPacket; 32],

    // FFT Data
    pub fft_data: [f32; 1024],
}

#[repr(C)]
pub struct ShmCmdBuffer {
    pub cmd_seq: u32,
    pub ack_seq: u32,
    pub cmd: [u8; 32],
    pub param_double: f64,
    pub param_int1: i32,
    pub param_int2: i32,
    pub param_int3: i32,
    pub param_str: [u8; 64],
}

#[derive(Serialize)]
pub struct ShmDeviceInfoResult {
    pub serial: String,
    pub name: String,
    pub index: i32,
}

#[derive(Serialize)]
pub struct ShmDecodedPacket {
    pub id: u32,
    pub timestamp: String,
    #[serde(rename = "freqOffsetKhz")]
    pub freq_offset_khz: f32,
    #[serde(rename = "syncWord")]
    pub sync_word: String,
    pub mask: String,
    #[serde(rename = "payloadHex")]
    pub payload_hex: String,
    #[serde(rename = "payloadAscii")]
    pub payload_ascii: String,
    #[serde(rename = "hwCrc")]
    pub hw_crc: String,
    #[serde(rename = "crcValid")]
    pub crc_valid: bool,
    pub score: f32,
    pub length: u16,
}

#[derive(Serialize)]
pub struct ShmStatusInfo {
    pub connected: bool,
    pub seq: u32,
    pub running: bool,
    #[serde(rename = "sampleRate")]
    pub sample_rate: f64,
    #[serde(rename = "centerFreq")]
    pub center_freq: f64,
    #[serde(rename = "lnaGain")]
    pub lna_gain: i32,
    #[serde(rename = "vgaGain")]
    pub vga_gain: i32,
    #[serde(rename = "ampEnable")]
    pub amp_enable: bool,
    #[serde(rename = "biasT")]
    pub bias_t: bool,
    pub source: String,
    #[serde(rename = "deviceSerial")]
    pub device_serial: String,
    pub devices: Vec<ShmDeviceInfoResult>,
    pub packets: Vec<ShmDecodedPacket>,
}

pub struct ShmManager {
    h_shm: isize,
    header_ptr: *const ShmHeader,
    h_cmd_shm: isize,
    cmd_ptr: *mut ShmCmdBuffer,
    last_packet_seq: u32,
    read_counter: u64,
}

unsafe impl Send for ShmManager {}
unsafe impl Sync for ShmManager {}

impl ShmManager {
    pub fn new() -> Self {
        let mut mgr = Self {
            h_shm: 0,
            header_ptr: std::ptr::null(),
            h_cmd_shm: 0,
            cmd_ptr: std::ptr::null_mut(),
            last_packet_seq: 0,
            read_counter: 0,
        };
        mgr.try_connect();
        mgr
    }

    pub fn try_connect(&mut self) -> bool {
        if !self.header_ptr.is_null() {
            return true;
        }

        unsafe {
            let h = OpenFileMappingW(FILE_MAP_READ, 0, SDRPP_SHM_NAME_W.as_ptr());
            if h != 0 {
                let p = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
                if !p.is_null() {
                    self.h_shm = h;
                    self.header_ptr = p as *const ShmHeader;
                    let hdr = &*self.header_ptr;
                    log_to_file("INFO", "Tauri/Rust SHM", &format!("Successfully mapped Shared Memory! Magic=0x{:X}, Initial Seq={}", hdr.magic, hdr.seq));
                } else {
                    CloseHandle(h);
                }
            }

            let h_cmd = OpenFileMappingW(FILE_MAP_ALL_ACCESS, 0, SDRPP_SHM_CMD_NAME_W.as_ptr());
            if h_cmd != 0 {
                let p_cmd = MapViewOfFile(h_cmd, FILE_MAP_ALL_ACCESS, 0, 0, 0);
                if !p_cmd.is_null() {
                    self.h_cmd_shm = h_cmd;
                    self.cmd_ptr = p_cmd as *mut ShmCmdBuffer;
                } else {
                    CloseHandle(h_cmd);
                }
            }
        }

        !self.header_ptr.is_null()
    }

    pub fn read_fft_f32(&mut self) -> Option<Vec<f32>> {
        if !self.try_connect() || self.header_ptr.is_null() {
            return None;
        }

        unsafe {
            let hdr = &*self.header_ptr;
            if hdr.magic != 0x53445250 {
                return None;
            }

            self.read_counter += 1;
            if self.read_counter % 120 == 1 {
                log_to_file("INFO", "Tauri/Rust SHM", &format!("IPC read_fft_f32: seq={}, running={}, sampleRate={}, fft[0]={:.1} dBm, fft[512]={:.1} dBm",
                    hdr.seq, hdr.running, hdr.sample_rate, hdr.fft_data[0], hdr.fft_data[512]));
            }

            let slice = std::slice::from_raw_parts(
                hdr.fft_data.as_ptr(),
                1024,
            );
            Some(slice.to_vec())
        }
    }

    pub fn read_status(&mut self) -> ShmStatusInfo {
        if !self.try_connect() || self.header_ptr.is_null() {
            return ShmStatusInfo {
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
            };
        }

        unsafe {
            let hdr = &*self.header_ptr;
            if hdr.magic != 0x53445250 {
                return ShmStatusInfo {
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
                };
            }

            let source_str = std::ffi::CStr::from_ptr(hdr.source_name.as_ptr() as *const i8)
                .to_string_lossy()
                .into_owned();
            let serial_str = std::ffi::CStr::from_ptr(hdr.device_serial.as_ptr() as *const i8)
                .to_string_lossy()
                .into_owned();

            // Extract physical devices list
            let mut dev_list = Vec::new();
            let dev_count = (hdr.device_count as usize).min(8);
            for i in 0..dev_count {
                let d = &hdr.devices[i];
                let d_serial = std::ffi::CStr::from_ptr(d.serial.as_ptr() as *const i8)
                    .to_string_lossy()
                    .into_owned();
                let d_name = std::ffi::CStr::from_ptr(d.name.as_ptr() as *const i8)
                    .to_string_lossy()
                    .into_owned();
                if !d_serial.is_empty() {
                    dev_list.push(ShmDeviceInfoResult {
                        serial: d_serial,
                        name: if d_name.is_empty() { "HackRF One".to_string() } else { d_name },
                        index: d.index,
                    });
                }
            }

            let mut new_packets = Vec::new();
            let cur_packet_seq = hdr.packet_seq;
            if cur_packet_seq > self.last_packet_seq {
                let count_to_read = (cur_packet_seq - self.last_packet_seq).min(32);
                let write_idx = hdr.packet_write_idx;

                for i in 0..count_to_read {
                    let idx = (write_idx + 32 - count_to_read + i) % 32;
                    let p = &hdr.packets[idx as usize];

                    let ts = std::ffi::CStr::from_ptr(p.timestamp.as_ptr() as *const i8)
                        .to_string_lossy()
                        .into_owned();

                    let p_len = p.payload_len as usize;
                    let mut hex_parts = Vec::new();
                    let mut ascii_parts = String::new();
                    for b in &p.payload[0..p_len.min(128)] {
                        hex_parts.push(format!("{:02X}", b));
                        ascii_parts.push(if *b >= 32 && *b <= 126 { *b as char } else { '.' });
                    }

                    new_packets.push(ShmDecodedPacket {
                        id: p.id,
                        timestamp: ts,
                        freq_offset_khz: p.freq_offset_khz,
                        sync_word: format!("0x{:08X}", p.sync_word),
                        mask: format!("0x{:02X}", p.mask),
                        payload_hex: hex_parts.join(" "),
                        payload_ascii: ascii_parts,
                        hw_crc: format!("0x{:08X}", p.hw_crc),
                        crc_valid: p.crc_valid != 0,
                        score: if p.crc_valid != 0 { 10.0 } else { 4.0 },
                        length: p.payload_len,
                    });
                }
                self.last_packet_seq = cur_packet_seq;
            }

            ShmStatusInfo {
                connected: true,
                seq: hdr.seq,
                running: hdr.running != 0,
                sample_rate: hdr.sample_rate,
                center_freq: hdr.center_freq,
                lna_gain: hdr.lna_gain,
                vga_gain: hdr.vga_gain,
                amp_enable: hdr.amp_enable != 0,
                bias_t: hdr.bias_t_enable != 0,
                source: if source_str.is_empty() { "HackRF".to_string() } else { source_str },
                device_serial: serial_str,
                devices: dev_list,
                packets: new_packets,
            }
        }
    }

    pub fn send_command(&mut self, cmd: &str, params: &Value) -> bool {
        if !self.try_connect() || self.cmd_ptr.is_null() {
            return false;
        }

        unsafe {
            let cmd_buf = &mut *self.cmd_ptr;
            cmd_buf.cmd = [0; 32];
            let cmd_bytes = cmd.as_bytes();
            let copy_len = cmd_bytes.len().min(31);
            cmd_buf.cmd[0..copy_len].copy_from_slice(&cmd_bytes[0..copy_len]);

            if let Some(f) = params.get("freq").and_then(|v| v.as_f64()) {
                cmd_buf.param_double = f;
            } else if let Some(sr) = params.get("sampleRate").and_then(|v| v.as_f64()) {
                cmd_buf.param_double = sr;
            }

            if let Some(lna) = params.get("lna").and_then(|v| v.as_i64()) {
                cmd_buf.param_int1 = lna as i32;
            }
            if let Some(vga) = params.get("vga").and_then(|v| v.as_i64()) {
                cmd_buf.param_int2 = vga as i32;
            }
            if let Some(amp) = params.get("amp").and_then(|v| v.as_bool()) {
                cmd_buf.param_int3 = if amp { 1 } else { 0 };
            }

            if let Some(src) = params.get("source").and_then(|v| v.as_str()) {
                cmd_buf.param_str = [0; 64];
                let s_bytes = src.as_bytes();
                let slen = s_bytes.len().min(63);
                cmd_buf.param_str[0..slen].copy_from_slice(&s_bytes[0..slen]);
            }

            log_to_file("INFO", "Tauri/Rust IPC", &format!("send_command: cmd='{}', params={}", cmd, params));
            cmd_buf.cmd_seq = cmd_buf.cmd_seq.wrapping_add(1);
        }

        true
    }
}

pub static GLOBAL_SHM: Mutex<Option<ShmManager>> = Mutex::new(None);
