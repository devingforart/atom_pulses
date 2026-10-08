fn main() {
    // sqlx::migrate! embeds migration bytes in the executable. Recompile this
    // crate whenever a new migration is added; applied files stay immutable.
    println!("cargo:rerun-if-changed=migrations");
}
