// Test-only driver of the unmodified, checksum-pinned Rust reference crate.
use shannon::Shannon;
use std::io::{self, BufRead};

fn unhex(text: &str) -> Vec<u8> {
    assert_eq!(text.len() % 2, 0);
    text.as_bytes().chunks(2).map(|pair| {
        u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap()
    }).collect()
}

fn hex(data: &[u8]) -> String {
    data.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn main() {
    for line in io::stdin().lock().lines() {
        let line = line.unwrap();
        let fields: Vec<_> = line.split('|').collect();
        assert_eq!(fields.len(), 5);
        let key = unhex(fields[1]);
        let nonce = unhex(fields[2]);
        let plain = unhex(fields[3]);
        let mut cipher = plain.clone();
        let mut ctx = Shannon::new(&key);
        ctx.nonce(&nonce);
        // Always contiguous: 0.2.0's process() has a buffered-byte MAC quirk.
        ctx.encrypt(&mut cipher);
        let mut mac = vec![0; fields[4].parse().unwrap()];
        ctx.finish(&mut mac);
        let mut rx = Shannon::new(&key);
        rx.nonce(&nonce);
        let mut decoded = cipher.clone();
        rx.decrypt(&mut decoded);
        assert_eq!(decoded, plain);
        rx.check_mac(&mac).unwrap();
        println!("{}|{}|{}", fields[0], hex(&cipher), hex(&mac));
    }
}
