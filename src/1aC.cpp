#include <iostream>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>

#define PORT 19036
#define BUF_SIZE 1024

using namespace std;

atomic<bool> running(true);

// ------------------- Listener thread -------------------
void listen_server(int sock) {
    char buffer[BUF_SIZE];

    while (running.load()) {
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (valread <= 0) {
            //cout << "[CLIENT] Disconnected from server.\n";
            running.store(false);
            break;
        }

        string msg(buffer);
        msg.erase(msg.find_last_not_of("\r\n") + 1);

        if (msg == "KEEP_ALIVE") {
            //cout << "[KEEP_ALIVE] Received ping from server" << endl;
            string reply = "ALIVE_OK";
            send(sock, reply.c_str(), reply.size(), 0);
            //cout << "[KEEP_ALIVE] Sent ALIVE_OK to server" << endl;
            continue;
        }

        // ---------------- Quiz Protocol Handling ----------------
        if (msg.rfind("QSTN|", 0) == 0) {
            // format: QSTN|question|A)|B)|C)|D)
            size_t p1 = msg.find("|", 5);
            if (p1 == string::npos) { cout << "[CLIENT] Invalid question.\n"; continue; }
            string question = msg.substr(5, p1 - 5);

            cout << "\n--- QUESTION ---\n" << question << endl;
            string opts = msg.substr(p1 + 1);
            size_t pos = 0;
            int i = 0;
            while ((pos = opts.find("|")) != string::npos && i < 3) {
                cout << opts.substr(0, pos) << endl;
                opts.erase(0, pos + 1);
                i++;
            }
            if (!opts.empty()) cout << opts << endl;
            cout << "---------------\n";
        }
        else if (msg.rfind("SCORE|", 0) == 0) {
            cout << "[CLIENT] " << msg.substr(6) << endl;
        }
        else if (msg.rfind("QUIZ_OVER|", 0) == 0) {
            cout << "\n=============================\n"
                 << "    " << msg.substr(10) << " \n"
                 << "=============================\n";
            running.store(false);
        }
        else if (msg.rfind("LEAD_RES|", 0) == 0) {
            cout << "\n--- LEADERBOARD ---\n";
            string data = msg.substr(9);
            size_t pos = 0;
            while ((pos = data.find(",")) != string::npos) {
                cout << data.substr(0, pos) << endl;
                data.erase(0, pos + 1);
            }
            if (!data.empty()) cout << data << endl;
            cout << "-------------------\n";
        }
        else if (msg.rfind("ERROR|", 0) == 0) {
            cout << "[CLIENT] Error: " << msg.substr(6) << endl;
        }
        else {
            cout << "[CLIENT] Server says: " << msg << endl;
        }
    }
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr{};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "192.168.50.111", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection Failed");
        return -1;
    }

    cout << "🎯 Connected to Quiz Server (port " << PORT << ")\n";
    cout << "Commands:\n"
         << "  genre <name>    - select quiz genre\n"
         << "  ans <A-D>       - answer current question\n"
         << "  next            - request next question\n"
         << "  lead            - request leaderboard\n"
         << "  exit            - quit client\n\n> ";

    // spawn listener
    thread listener(listen_server, sock);

    // input loop
    string input;
    while (running.load() && getline(cin, input)) {
        if (input.empty()) continue;

        if (input.rfind("genre ", 0) == 0) {
            string genre = input.substr(6);
            string msg = "GENRE|" + genre;
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input.rfind("ans ", 0) == 0) {
            char ans = toupper(input[4]);
            string msg = "ANSR|";
            msg.push_back(ans);
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input == "next") {
            string msg = "NEXT_Q";
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input == "lead") {
            string msg = "LEAD_REQ";
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input == "exit") {
            string msg = "exit";
            send(sock, msg.c_str(), msg.size(), 0);
            running.store(false);
            break;
        }
        else {
            cout << "[CLIENT] Unknown command.\n";
        }

        cout << "> ";
    }

    if (listener.joinable()) listener.join();
    close(sock);
    return 0;
}
