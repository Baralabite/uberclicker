fn main() {
    let attributes = tauri_build::Attributes::new();
    #[cfg(windows)]
    let attributes = {
        // Embed in test executables too: https://github.com/tauri-apps/tauri/issues/13419
        let manifest = std::env::current_dir()
            .unwrap()
            .join("windows-app-manifest.xml");
        println!("cargo:rerun-if-changed={}", manifest.display());
        println!("cargo:rustc-link-arg=/MANIFEST:EMBED");
        println!("cargo:rustc-link-arg=/MANIFESTINPUT:{}", manifest.display());
        println!("cargo:rustc-link-arg=/WX");
        attributes.windows_attributes(tauri_build::WindowsAttributes::new_without_app_manifest())
    };
    tauri_build::try_build(attributes).expect("failed to build Tauri resources");
}
