#!/usr/bin/env python3
import matplotlib.pyplot as plt
import pandas as pd

# ---- CONFIG ----
CLIENT_COUNTS = [1, 2, 4, 6, 8, 10]

# Latencies (GENRE→QSTN) from your Part 1B plot (in seconds, approx values read from graph)
LATENCIES = [18, 9, 10, 10.5, 7.2, 6.2]

QUESTIONS_PER_CLIENT = 5
BYTES_PER_QUESTION = 300  # assume each question ~300 bytes
# ----------------

def estimate_throughput(latencies, client_counts, q_per_client, bytes_per_q):
    results = []
    for n, lat in zip(client_counts, latencies):
        total_bytes = n * q_per_client * bytes_per_q
        throughput = total_bytes / lat if lat > 0 else 0
        results.append(throughput)
    return results

def main():
    throughputs = estimate_throughput(LATENCIES, CLIENT_COUNTS,
                                      QUESTIONS_PER_CLIENT, BYTES_PER_QUESTION)

    df = pd.DataFrame({
        "clients": CLIENT_COUNTS,
        "latency": LATENCIES,
        "throughput_bytes_per_sec": throughputs
    })
    df.to_csv("estimated_throughput_part1B.csv", index=False)
    print(df)

    # Plot
    plt.plot(CLIENT_COUNTS, throughputs, marker='o', color="green")
    plt.title("Estimated Throughput vs Clients (Part 1B)")
    plt.xlabel("Number of Clients")
    plt.ylabel("Throughput (bytes/sec)")
    plt.grid(True)
    plt.savefig("estimated_throughput_part1B.png")
    print("[INFO] Saved estimated_throughput_part1B.png")

if __name__ == "__main__":
    main()
