#include <iostream>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <sstream>
#include <cctype>
#include <chrono>
#include <mutex>
#include <netdb.h>

#define PORT 19036
#define MAX_CLIENTS 10

using namespace std;

struct Question {
    string q;
    string options[4];
    char answer;
};

atomic<int> client_count(0);
map<int, int> scores;
map<int, int> current_q;
map<int, vector<Question>> client_quizzes;

mutex quiz_mutex;

// --------------------- Helpers ------------------------
void trim(string &s) {
    while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
}

void send_msg(int sock, const string &msg) {
    string out = msg + "\n";
    send(sock, out.c_str(), out.size(), 0);
}

void send_question(int client_socket) {
    lock_guard<mutex> lock(quiz_mutex);
    int q_index = current_q[client_socket];
    auto &quiz = client_quizzes[client_socket];
    if (q_index < (int)quiz.size()) {
        Question &q = quiz[q_index];
        string out = "QSTN|" + q.q + "|" + q.options[0] + "|" +
                     q.options[1] + "|" + q.options[2] + "|" + q.options[3];
        send_msg(client_socket, out);
    } else {
        send_msg(client_socket, "QUIZ_OVER|🎉 Final Score: " +
                                to_string(scores[client_socket]) + " 🎉");
    }
}

// quiz generation llm
vector<Question> generateQuiz(const string &genre) {
    vector<Question> qs;

    //reserve
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) { perror("socket"); return qs; }

        struct hostent *server = gethostbyname("192.168.50.142");
        if (!server) { cerr << "[LLM] No such host.\n"; close(sockfd); return qs; }

        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(25000);
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

        if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            perror("[LLM] connect");
            close(sockfd);
            return qs;
        }

        string reserve_body = "{ \"rollno\":\"es23btech11036\" }";
        string reserve_http = "POST /reserve HTTP/1.1\r\n"
                              "Host: 192.168.50.142:25000\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + to_string(reserve_body.size()) + "\r\n\r\n" +
                              reserve_body;

        send(sockfd, reserve_http.c_str(), reserve_http.size(), 0);

        char buf[2048];
        int n = read(sockfd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            cout << "[LLM] Reserve response: " << buf << endl;
        }
        close(sockfd);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return qs; }

    struct hostent *server = gethostbyname("192.168.50.142");
    if (!server) { cerr << "[LLM] No such host.\n"; close(sockfd); return qs; }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(25000);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[LLM] connect");
        close(sockfd);
        return qs;
    }

    //prompt
    string body =
        "{"
        "\"rollno\":\"es23btech11036\","
        "\"messages\":["
        "{"
        "\"role\":\"system\","
        "\"content\":\"You are a trivia generator. Given a genre, output exactly 10 multiple-choice questions. "
        "Each question must have 4 options (A, B, C, D) and one correct answer. "
        "Return ONLY valid plain text in the following format: "
        "1. Question text\\nA) ...\\nB) ...\\nC) ...\\nD) ...\\nAnswer: X\""
        "},"
        "{"
        "\"role\":\"user\","
        "\"content\":\"You are a trivia generator. Output exactly 10 multiple-choice questions of " + genre + ". "
        "Each question must have 4 options (A, B, C, D) and one correct answer. "
        "Return ONLY valid plain text in the following format: "
        "1. Question text\\nA) ...\\nB) ...\\nC) ...\\nD) ...\\nAnswer: X\""
        "}"
        "],"
        "\"temperature\":0.0,"
        "\"max_tokens\":1200"
        "}";

    string http = "POST /generate_quiz HTTP/1.1\r\n"
                  "Host: 192.168.50.142:25000\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: " + to_string(body.size()) + "\r\n\r\n" + body;

    send(sockfd, http.c_str(), http.size(), 0);

    // read response
    string response;
    char buf[8192];
    int n;
    while ((n = read(sockfd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        response += buf;
    }
    close(sockfd);

    //Extract JSON body
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == string::npos) return qs;
    string json = response.substr(header_end + 4);

    // Extract "content" field
    size_t pos = json.find("\"content\"");
    if (pos == string::npos) return qs;
    pos = json.find(":", pos);
    if (pos == string::npos) return qs;
    size_t start = json.find("\"", pos);
    size_t end   = json.find("\"role", start);
    if (start == string::npos || end == string::npos || end <= start) return qs;

    string quizText = json.substr(start + 1, end - start - 3);

    // add newlines
    for (size_t pos2 = 0; (pos2 = quizText.find("\\n", pos2)) != string::npos; ) {
        quizText.replace(pos2, 2, "\n");
    }

    cout << "[DEBUG] Extracted quizText:\n" << quizText << endl;

    // parser logic
    istringstream iss(quizText);
    string line;
    Question q;
    int optionCount = 0;

    while (getline(iss, line)) {
        trim(line);
        if (line.empty()) continue;

        if (isdigit(line[0])) {
            // push previous question if valid
            if (!q.q.empty() && optionCount == 4 && q.answer != '\0') {
                qs.push_back(q);
            }
            q = Question(); // reset
            optionCount = 0;
            q.answer = '\0';
            q.q = line.substr(line.find('.') + 1);
            trim(q.q);
        }
        else if ((line[0] == 'A' || line[0] == 'B' ||
                  line[0] == 'C' || line[0] == 'D') && line[1] == ')') {
            if (optionCount < 4) {
                q.options[optionCount] = line;
                optionCount++;
            }
        }
        else if (line.rfind("Answer:", 0) == 0) {
            if (!line.empty()) {
                q.answer = line.back();
            }
        }
    }

    // push last question if valid
    if (!q.q.empty() && optionCount == 4 && q.answer != '\0') {
        qs.push_back(q);
    }

    cout << "[INFO] Parsed " << qs.size() << " questions.\n";
    return qs;
}

// client handler
void handle_client(int client_socket) {
    client_count++;
    scores[client_socket] = 0;
    current_q[client_socket] = 0;

    atomic<bool> client_is_alive(true);
    mutex last_msg_mutex;
    auto last_message_time = chrono::steady_clock::now();

    // keep alive
    thread keep_alive_th([&]() {
        while (client_is_alive.load()) {
            send_msg(client_socket, "KEEP_ALIVE");
            cout << "[KEEP_ALIVE] Sent ping -> socket " << client_socket << endl;

            this_thread::sleep_for(chrono::seconds(5));

            auto now = chrono::steady_clock::now();
            chrono::steady_clock::time_point last;
            {
                lock_guard<mutex> lg(last_msg_mutex);
                last = last_message_time;
            }
            auto elapsed = chrono::duration_cast<chrono::seconds>(now - last).count();
            if (elapsed > 5) {
                cout << "[SERVER] Client timed out: socket " << client_socket << endl;
                send_msg(client_socket, "QUIZ_OVER|Timeout - disconnected");
                client_is_alive.store(false);
                break;
            }
        }
    });

    // main handler
    char buffer[1024];
    while (client_is_alive.load()) {
        memset(buffer, 0, sizeof(buffer));
        int valread = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (valread <= 0) {
            cout << "[SERVER] Client disconnected (socket " << client_socket << ")\n";
            break;
        }

        string msg(buffer);
        msg.erase(msg.find_last_not_of("\r\n") + 1);

        {
            lock_guard<mutex> lg(last_msg_mutex);
            last_message_time = chrono::steady_clock::now();
        }

        if (msg == "ALIVE_OK") {
            cout << "[KEEP_ALIVE] Pong from client " << client_socket << endl;
            continue;
        }

        cout << "[SERVER] Received: " << msg << endl;

        if (msg.rfind("GENRE|", 0) == 0) {
            string genre = msg.substr(6);
            cout << "[SERVER] Generating quiz for genre: " << genre << endl;

            // spawn worker thread for quiz generation
            thread([client_socket, genre]() {
                vector<Question> qs = generateQuiz(genre);
                {
                    lock_guard<mutex> lock(quiz_mutex);
                    client_quizzes[client_socket] = qs;
                    current_q[client_socket] = 0;
                    scores[client_socket] = 0;
                }
                send_question(client_socket);
            }).detach();
        }
        else if (msg.rfind("ANSR|", 0) == 0) {
            char ans = msg[5];
            lock_guard<mutex> lock(quiz_mutex);
            auto &quiz = client_quizzes[client_socket];
            int q_index = current_q[client_socket];
            if (q_index < (int)quiz.size()) {
                if (ans == quiz[q_index].answer) {
                    scores[client_socket]++;
                    send_msg(client_socket, "SCORE|Correct");
                } else {
                    send_msg(client_socket, "SCORE|Wrong");
                }
            } else {
                send_msg(client_socket, "QUIZ_OVER|No more questions");
            }
        }
        else if (msg == "NEXT_Q") {
            current_q[client_socket]++;
            send_question(client_socket);
        }
        else if (msg == "LEAD_REQ") {
            string reply = "LEAD_RES|";
            bool first = true;
            for (auto &kv : scores) {
                if (!first) reply += ",";
                reply += "Client" + to_string(kv.first) + ":" + to_string(kv.second);
                first = false;
            }
            send_msg(client_socket, reply);
        }
        else if (msg == "exit") {
            cout << "[SERVER] Client requested exit.\n";
            break;
        }
        else {
            send_msg(client_socket, "ERROR|Unknown command");
        }
    }

    // cleanup
    client_is_alive.store(false);
    if (keep_alive_th.joinable()) keep_alive_th.join();

    close(client_socket);
    {
        lock_guard<mutex> lock(quiz_mutex);
        scores.erase(client_socket);
        current_q.erase(client_socket);
        client_quizzes.erase(client_socket);
    }
    client_count--;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address{};
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return -1;
    }

    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    cout << "[SERVER] Listening on port " << PORT << "...\n";

    while (true) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        if (client_count >= MAX_CLIENTS) {
            const char *msg = "Server full. Try again later.\n";
            send(new_socket, msg, strlen(msg), 0);
            close(new_socket);
            continue;
        }

        cout << "[SERVER] New client connected: "
             << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port)
             << " | Active clients: " << client_count + 1 << endl;

        thread(handle_client, new_socket).detach();
    }

    close(server_fd);
    return 0;
}
