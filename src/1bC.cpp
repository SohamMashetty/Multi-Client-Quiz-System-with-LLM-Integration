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

void listen_server(int sock) {
    char buffer[BUF_SIZE];

    while (running.load()) {
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(sock, buffer, BUF_SIZE - 1, 0);
        if (valread <= 0) {
            cout << "[CLIENT] Disconnected.\n";
            running.store(false);
            break;
        }

        string msg(buffer);
        msg.erase(msg.find_last_not_of("\r\n") + 1);

        if (msg == "KEEP_ALIVE") {
            string reply = "ALIVE_OK";
            send(sock, reply.c_str(), reply.size(), 0);
            continue;
        }

        if (msg.rfind("CACHE_HIT|", 0) == 0) {
            cout << "[CLIENT] Cached questions used for genre "
                 << msg.substr(10) << endl;
        }
        else if (msg.rfind("CACHE_MISS|", 0) == 0) {
            cout << "[CLIENT] Fresh questions generated for genre "
                 << msg.substr(11) << endl;
        }
        else if (msg.rfind("QSTN|", 0) == 0) {
            size_t p1 = msg.find("|", 5);
            string question = msg.substr(5, p1 - 5);
            cout << "\n--- QUESTION ---\n" << question << endl;
            string opts = msg.substr(p1 + 1);
            size_t pos;
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
                 << "   🎉 " << msg.substr(10) << " 🎉\n"
                 << "=============================\n";
            running.store(false);
        }
        else if (msg.rfind("LEAD_RES|", 0) == 0) {
            cout << "\n--- LEADERBOARD ---\n";
            string data = msg.substr(9);
            size_t pos;
            while ((pos = data.find(",")) != string::npos) {
                cout << data.substr(0, pos) << endl;
                data.erase(0, pos + 1);
            }
            if (!data.empty()) cout << data << endl;
            cout << "-------------------\n";
        }
        else {
            cout << "[CLIENT] Server: " << msg << endl;
        }
    }
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr{};

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "192.168.50.111", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        return -1;
    }

    cout << "🎯 Connected to Quiz Server (port " << PORT << ")\n";
    cout << "Commands:\n"
         << "  genre <name>\n"
         << "  ans <A-D>\n"
         << "  next\n"
         << "  lead\n"
         << "  exit\n\n> ";

    thread listener(listen_server, sock);

    string input;
    while (running.load() && getline(cin, input)) {
        if (input.empty()) continue;
        if (input.rfind("genre ", 0) == 0) {
            string msg = "GENRE|" + input.substr(6);
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input.rfind("ans ", 0) == 0) {
            string msg = "ANSR|";
            msg.push_back(toupper(input[4]));
            send(sock, msg.c_str(), msg.size(), 0);
        }
        else if (input == "next") send(sock, "NEXT_Q", 6, 0);
        else if (input == "lead") send(sock, "LEAD_REQ", 8, 0);
        else if (input == "exit") {
            send(sock, "exit", 4, 0);
            running.store(false);
            break;
        }
        cout << "> ";
    }

    if (listener.joinable()) listener.join();
    close(sock);
    return 0;
}
