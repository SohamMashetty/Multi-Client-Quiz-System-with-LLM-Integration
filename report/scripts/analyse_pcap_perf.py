#!/usr/bin/env python3
import os, sys, subprocess, binascii
import pandas as pd
import matplotlib.pyplot as plt

# ---- CONFIG ----
CLIENT_IP = "172.21.148.122"  # replace with YOUR client machine IP
CLIENT_COUNTS = [1, 2, 4, 6, 8, 10]  # concurrency levels you tested
PART = "1B"  # or "1B"
# ----------------

def run_tshark_conv(pcap_file):
    """Run tshark conv to extract bytes & duration (for throughput)."""
    cmd = ["tshark", "-r", pcap_file, "-q", "-z", "conv,tcp"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    total_bytes = 0
    duration = 0
    for line in result.stdout.splitlines():
        if CLIENT_IP in line:  # look at the client stream
            parts = line.split()
            try:
                bytes_sent = int(parts[6])
                bytes_recv = int(parts[7])
                total_bytes = bytes_sent + bytes_recv
                duration = float(parts[8])
            except:
                continue
    if duration > 0:
        return total_bytes / duration
    return 0

def run_tshark_events(pcap_file):
    """Extract quiz protocol messages (GENRE, QSTN, ANSR, SCORE)."""
    cmd = [
        "tshark", "-r", pcap_file,
        "-Y", 'tcp contains "GENRE|" or tcp contains "QSTN|" or tcp contains "ANSR|" or tcp contains "SCORE|"',
        "-T", "fields",
        "-e", "frame.time_epoch", "-e", "ip.src", "-e", "ip.dst", "-e", "data.data"
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    rows = []
    for line in result.stdout.splitlines():
        parts = line.strip().split("\t")
        if len(parts) < 4: continue
        ts, src, dst, payload_hex = parts
        try:
            payload = binascii.unhexlify(payload_hex).decode(errors="ignore")
        except:
            payload = ""
        rows.append((float(ts), src, dst, payload))
    return pd.DataFrame(rows, columns=["time","src","dst","payload"])

def compute_latencies(df):
    """Compute GENRE->QSTN and ANSR->SCORE latencies."""
    quiz_lat = []
    ans_lat = []
    genre_ts = None
    ans_ts = None

    for _, row in df.iterrows():
        msg = row["payload"]
        ts = row["time"]

        if "GENRE|" in msg and row["src"] == CLIENT_IP:
            genre_ts = ts
        if "QSTN|" in msg and row["dst"] == CLIENT_IP and genre_ts:
            quiz_lat.append(ts - genre_ts)
            genre_ts = None

        if "ANSR|" in msg and row["src"] == CLIENT_IP:
            ans_ts = ts
        if "SCORE|" in msg and row["dst"] == CLIENT_IP and ans_ts:
            ans_lat.append(ts - ans_ts)
            ans_ts = None

    return (sum(quiz_lat)/len(quiz_lat) if quiz_lat else 0,
            sum(ans_lat)/len(ans_lat) if ans_lat else 0)

def main():
    results = []
    for n in CLIENT_COUNTS:
        pcap_file = f"part{PART}_client_{n}.pcap"
        if not os.path.exists(pcap_file):
            print(f"[WARN] Missing {pcap_file}, skipping...")
            continue

        print(f"[INFO] Processing {pcap_file} ...")

        throughput = run_tshark_conv(pcap_file)
        df = run_tshark_events(pcap_file)
        quiz_lat, ans_lat = compute_latencies(df)

        results.append({
            "clients": n,
            "throughput_bps": throughput,
            "quiz_latency": quiz_lat,
            "ans_latency": ans_lat
        })

    df_res = pd.DataFrame(results)
    df_res.to_csv(f"results_part{PART}.csv", index=False)
    print("[INFO] Results saved to", f"results_part{PART}.csv")

    # ---- Plotting ----
    plt.figure(figsize=(10,5))
    plt.plot(df_res["clients"], df_res["throughput_bps"], marker='o')
    plt.title(f"Throughput vs Clients (Part {PART})")
    plt.xlabel("Number of Clients")
    plt.ylabel("Throughput (bytes/sec)")
    plt.grid(True)
    plt.savefig(f"throughput_part{PART}.png")
    print("[INFO] Saved throughput_part{PART}.png")

    plt.figure(figsize=(10,5))
    plt.plot(df_res["clients"], df_res["quiz_latency"], marker='o', label="GENRE→QSTN")
    plt.plot(df_res["clients"], df_res["ans_latency"], marker='s', label="ANSR→SCORE")
    plt.title(f"Latency vs Clients (Part {PART})")
    plt.xlabel("Number of Clients")
    plt.ylabel("Latency (seconds)")
    plt.legend()
    plt.grid(True)
    plt.savefig(f"latency_part{PART}.png")
    print("[INFO] Saved latency_part{PART}.png")

if __name__ == "__main__":
    main()
