fn main() {
    let mut attrs = tauri_build::Attributes::new();
    let mut win_attrs = tauri_build::WindowsAttributes::new();

    let ascii_icon = std::path::PathBuf::from("C:/Software/tauri_target/icon.ico");
    if let Ok(icon_bytes) = std::fs::read("icons/icon.ico") {
        let _ = std::fs::create_dir_all("C:/Software/tauri_target");
        let _ = std::fs::write(&ascii_icon, icon_bytes);
        win_attrs = win_attrs.window_icon_path("C:/Software/tauri_target/icon.ico");
    }

    attrs = attrs.windows_attributes(win_attrs);
    tauri_build::try_build(attrs).expect("failed to build tauri");
}
