// ============================================================
//  ATM SIMULATOR v3 - C++ backend (HTTP server) + web frontend
//  No external libraries: uses plain sockets (Linux/Mac/Windows).
//
//  Build (Linux/Mac): g++ -std=c++14 atm_server.cpp -o atm_server
//  Build (Windows)  : g++ -std=c++14 atm_server.cpp -o atm_server -lws2_32
//  Run              : ./atm_server   then open http://localhost:8080
//
//  OOP: Person (abstract) -> User, Admin | Transaction (abstract) ->
//       Deposit, Withdrawal, PinChange | Card | ATMServer
// ============================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <deque>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <cctype>
using namespace std;

#ifdef _WIN32
  #include <winsock2.h>
  typedef SOCKET sock_t;
  typedef int socklen_t_;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define closesocket close
#endif

string stamp(const char* fmt) {
    time_t t = time(nullptr); char b[32];
    strftime(b, sizeof b, fmt, localtime(&t));
    return b;
}

// ---------------- Person (abstract) ----------------
class Person {
protected: string name;
public:
    Person(string n) : name(n) {}
    Person(const Person&) = default;
    Person(Person&&) noexcept = default;
    virtual ~Person() {}
    string getName() const { return name; }
    virtual string role() const = 0;
};

// ---------------- Card ----------------
class Card {
    string number; int pin, failed; bool blocked;
public:
    Card(string n = "", int p = 0) : number(n), pin(p), failed(0), blocked(false) {}
    string getNumber() const { return number; }
    bool isBlocked() const { return blocked; }
    int attemptsLeft() const { return 3 - failed; }
    bool verifyPin(int e) {
        if (blocked) return false;
        if (e == pin) { failed = 0; return true; }
        if (++failed >= 3) blocked = true;
        return false;
    }
    void changePin(int p) { pin = p; }
    void unblock() { blocked = false; failed = 0; }
};

// ---------------- Transaction (abstract) ----------------
class User;
class Transaction {
protected:
    string type, ts, msg; double amount; bool success;
    bool fail(const string& m) { msg = m; return success = false; }
    bool done(const string& m) { msg = m; return success = true; }
public:
    Transaction(string t, double a) : type(t), ts(stamp("%d-%m-%Y %H:%M")), amount(a), success(false) {}
    virtual ~Transaction() {}
    virtual bool execute(User& u) = 0;
    string message() const { return msg; }
    string describe() const {
        return ts + " | " + type + " | Rs." + to_string((long long)amount) + " | " + (success ? "OK" : "FAILED");
    }
};

// ---------------- User ----------------
class User : public Person {
    double balance, dailyLimit, withdrawnToday; Card card; string lastDate;
    vector<unique_ptr<Transaction>> history;
    void roll() { string t = stamp("%Y-%m-%d"); if (t != lastDate) { lastDate = t; withdrawnToday = 0; } }
public:
    User(string n, double b, Card c, double lim = 25000)
        : Person(n), balance(b), dailyLimit(lim), withdrawnToday(0), card(c), lastDate(stamp("%Y-%m-%d")) {}
    User(User&&) noexcept = default;
    string role() const override { return "Customer"; }
    double getBalance() const { return balance; }
    double getLimit() const { return dailyLimit; }
    void setLimit(double l) { dailyLimit = l; }
    double remaining() { roll(); return dailyLimit - withdrawnToday; }
    void noteWithdrawal(double a) { roll(); withdrawnToday += a; }
    Card& getCard() { return card; }
    void credit(double a) { balance += a; }
    bool debit(double a) { if (a > balance) return false; balance -= a; return true; }
    bool perform(unique_ptr<Transaction> t, string& msg) {   // polymorphism
        bool ok = t->execute(*this);
        msg = t->message();
        history.push_back(move(t));
        return ok;
    }
    vector<string> recent(size_t n = 5) const {
        vector<string> v;
        for (size_t i = history.size() > n ? history.size() - n : 0; i < history.size(); i++)
            v.push_back(history[i]->describe());
        return v;
    }
};

class Deposit : public Transaction {
public:
    Deposit(double a) : Transaction("DEPOSIT", a) {}
    bool execute(User& u) override {
        if (amount <= 0) return fail("Enter an amount greater than zero.");
        u.credit(amount);
        return done("Rs. " + to_string((long long)amount) + " deposited.");
    }
};
class Withdrawal : public Transaction {
public:
    Withdrawal(double a) : Transaction("WITHDRAW", a) {}
    bool execute(User& u) override {
        if (amount <= 0 || (long long)amount % 100 != 0) return fail("Amount must be a multiple of 100.");
        if (amount > 20000) return fail("Limit per transaction is Rs. 20000.");
        if (amount > u.remaining()) return fail("Daily limit exceeded. Left today: Rs. " + to_string((long long)u.remaining()));
        if (!u.debit(amount)) return fail("Insufficient balance.");
        u.noteWithdrawal(amount);
        return done("Collect your cash: Rs. " + to_string((long long)amount));
    }
};
class PinChange : public Transaction {
    int oldP, newP;
public:
    PinChange(int o, int n) : Transaction("PIN CHANGE", 0), oldP(o), newP(n) {}
    bool execute(User& u) override {
        if (newP < 1000 || newP > 9999) return fail("New PIN must be 4 digits.");
        if (!u.getCard().verifyPin(oldP)) return fail("Old PIN is incorrect.");
        u.getCard().changePin(newP);
        return done("PIN changed.");
    }
};

// ---------------- Admin ----------------
class Admin : public Person {
    string password;
public:
    Admin(string n, string p) : Person(n), password(p) {}
    string role() const override { return "Administrator"; }
    bool authenticate(const string& u, const string& p) const { return u == name && p == password; }
};

// ---------------- JSON helpers (tiny, flat objects only) ----------------
string esc(const string& s) {
    string o;
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') o += "\\n"; else o += c; }
    return o;
}
string jget(const string& b, const string& k) {
    size_t p = b.find("\"" + k + "\"");
    if (p == string::npos || (p = b.find(':', p)) == string::npos) return "";
    p++;
    while (p < b.size() && isspace((unsigned char)b[p])) p++;
    string v;
    if (p < b.size() && b[p] == '"') {
        for (p++; p < b.size() && b[p] != '"'; p++) { if (b[p] == '\\' && p + 1 < b.size()) p++; v += b[p]; }
    } else {
        while (p < b.size() && b[p] != ',' && b[p] != '}' && !isspace((unsigned char)b[p])) v += b[p++];
    }
    return v;
}
bool num(const string& s, long long& v) {
    try { size_t p; v = stoll(s, &p); return p == s.size(); } catch (...) { return false; }
}
string R(bool ok, const string& msg, const string& extra = "") {
    return string("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + esc(msg) + "\"" + extra + "}";
}

// ---------------- ATM server logic ----------------
class ATMServer {
    deque<User> users;               // deque: pointers stay valid when adding users
    Admin admin;
    map<string, User*> sessions;
    set<string> adminTokens;

    User* find(const string& card) {
        for (auto& u : users) if (u.getCard().getNumber() == card) return &u;
        return nullptr;
    }
    string token() {
        static const char* h = "0123456789abcdef"; string t;
        for (int i = 0; i < 24; i++) t += h[rand() % 16];
        return t;
    }
    string info(User& u) {
        return ",\"name\":\"" + esc(u.getName()) + "\",\"balance\":" + to_string((long long)u.getBalance()) +
               ",\"dailyLimit\":" + to_string((long long)u.getLimit()) +
               ",\"remaining\":" + to_string((long long)u.remaining());
    }
public:
    ATMServer() : admin("admin", "admin123") {
        users.emplace_back("Aarav Sharma", 25000, Card("1111", 1234));
        users.emplace_back("Priya Singh", 50000, Card("2222", 4321));
        users.emplace_back("Rohan Verma", 10000, Card("3333", 1111));
    }

    string handle(const string& path, const string& b) {
        string tk = jget(b, "token");

        // ----- public -----
        if (path == "/api/login") {
            User* u = find(jget(b, "card")); long long pin;
            if (!u) return R(false, "Card not recognised.");
            if (u->getCard().isBlocked()) return R(false, "This card is blocked. Please contact the admin.", ",\"blocked\":true");
            if (num(jget(b, "pin"), pin) && u->getCard().verifyPin((int)pin)) {
                string t = token(); sessions[t] = u;
                return R(true, "Welcome", ",\"token\":\"" + t + "\"" + info(*u));
            }
            if (u->getCard().isBlocked()) return R(false, "Too many wrong attempts. Card blocked.", ",\"blocked\":true");
            return R(false, "Wrong PIN. Attempts left: " + to_string(u->getCard().attemptsLeft()));
        }
        if (path == "/api/admin/login") {
            if (!admin.authenticate(jget(b, "username"), jget(b, "password"))) return R(false, "Invalid admin credentials.");
            string t = token(); adminTokens.insert(t);
            return R(true, "Welcome, " + admin.getName(), ",\"token\":\"" + t + "\"");
        }
        if (path == "/api/logout") { sessions.erase(tk); adminTokens.erase(tk); return R(true, "Logged out."); }

        // ----- admin -----
        if (path.rfind("/api/admin/", 0) == 0) {
            if (!adminTokens.count(tk)) return R(false, "Admin login required.");
            if (path == "/api/admin/users") {
                string j = ",\"users\":[";
                for (size_t i = 0; i < users.size(); i++) {
                    User& u = users[i];
                    j += string(i ? "," : "") + "{\"card\":\"" + u.getCard().getNumber() + "\",\"name\":\"" + esc(u.getName()) +
                         "\",\"balance\":" + to_string((long long)u.getBalance()) + ",\"limit\":" + to_string((long long)u.getLimit()) +
                         ",\"blocked\":" + (u.getCard().isBlocked() ? "true" : "false") + "}";
                }
                return R(true, "OK", j + "]");
            }
            if (path == "/api/admin/unblock") {
                User* u = find(jget(b, "card"));
                if (!u) return R(false, "Card not found.");
                u->getCard().unblock();
                return R(true, "Card unblocked for " + u->getName() + ".");
            }
            if (path == "/api/admin/setlimit") {
                User* u = find(jget(b, "card")); long long l;
                if (!u) return R(false, "Card not found.");
                if (!num(jget(b, "limit"), l) || l < 0) return R(false, "Enter a valid limit.");
                u->setLimit((double)l);
                return R(true, "Daily limit for " + u->getName() + " is now Rs. " + to_string(l) + ".");
            }
            if (path == "/api/admin/adduser") {
                string n = jget(b, "name"), c = jget(b, "card"); long long p, bal;
                if (n.empty() || c.empty()) return R(false, "Name and card number are required.");
                if (find(c)) return R(false, "That card number already exists.");
                if (!num(jget(b, "pin"), p) || p < 1000 || p > 9999) return R(false, "PIN must be 4 digits.");
                if (!num(jget(b, "balance"), bal) || bal < 0) return R(false, "Enter a valid opening balance.");
                users.emplace_back(n, (double)bal, Card(c, (int)p));
                return R(true, "Account created for " + n + ".");
            }
            return R(false, "Unknown admin action.");
        }

        // ----- customer (needs session) -----
        auto it = sessions.find(tk);
        if (it == sessions.end()) return R(false, "Session expired. Please insert your card again.");
        User& u = *it->second; long long a, o, n; string msg;

        if (path == "/api/info") return R(true, "OK", info(u));
        if (path == "/api/statement") {
            string j = ",\"items\":[";
            auto v = u.recent();
            for (size_t i = 0; i < v.size(); i++) j += string(i ? "," : "") + "\"" + esc(v[i]) + "\"";
            return R(true, "OK", j + "]" + info(u));
        }
        if (path == "/api/deposit" || path == "/api/withdraw") {
            if (!num(jget(b, "amount"), a)) return R(false, "Enter a valid amount.");
            bool ok = path == "/api/deposit" ? u.perform(make_unique<Deposit>((double)a), msg)
                                             : u.perform(make_unique<Withdrawal>((double)a), msg);
            return R(ok, msg, info(u));
        }
        if (path == "/api/changepin") {
            if (!num(jget(b, "oldPin"), o) || !num(jget(b, "newPin"), n)) return R(false, "Enter both PINs as numbers.");
            bool ok = u.perform(make_unique<PinChange>((int)o, (int)n), msg);
            return R(ok, msg, info(u));
        }
        return R(false, "Unknown request.");
    }
};

// ---------------- Minimal HTTP server ----------------
string httpResp(int code, const string& type, const string& body) {
    return "HTTP/1.1 " + to_string(code) + (code == 200 ? " OK" : " Not Found") + "\r\nContent-Type: " + type +
           "\r\nContent-Length: " + to_string(body.size()) +
           "\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type"
           "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nConnection: close\r\n\r\n" + body;
}
void sendAll(sock_t c, const string& s) {
    size_t off = 0;
    while (off < s.size()) {
        int n = send(c, s.data() + off, (int)(s.size() - off), 0);
        if (n <= 0) break;
        off += n;
    }
}

int main() {
    srand((unsigned)time(nullptr));
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
#endif
    const int PORT = 8080;
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);        // local machine only
    addr.sin_port = htons(PORT);
    if (::bind(s, (sockaddr*)&addr, sizeof addr) < 0 || ::listen(s, 10) < 0) {   // ::bind avoids clash with std::bind
        cerr << "Could not start server on port " << PORT << ". Another program (or an old atm_server) is probably using it.\n"
             << "Close it, or change PORT in the code and rebuild.\n";
        return 1;
    }
    cout << "ATM server running -> open http://localhost:" << PORT << "  (Ctrl+C to stop)\n";

    ATMServer atm;
    while (true) {
        sock_t c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;

        string req; char buf[4096]; size_t he;
        while ((he = req.find("\r\n\r\n")) == string::npos) {
            int n = recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, n);
        }
        if (he == string::npos) { closesocket(c); continue; }

        size_t cl = 0, p = req.find("Content-Length:");
        if (p == string::npos) p = req.find("content-length:");
        if (p != string::npos && p < he) cl = (size_t)atoi(req.c_str() + p + 15);
        while (req.size() < he + 4 + cl) {
            int n = recv(c, buf, sizeof buf, 0);
            if (n <= 0) break;
            req.append(buf, n);
        }
        string body = req.substr(he + 4, cl);
        istringstream rl(req); string method, path; rl >> method >> path;
        cout << method << " " << path << "\n";

        if (method == "OPTIONS") sendAll(c, httpResp(200, "text/plain", ""));
        else if (method == "GET" && (path == "/" || path == "/index.html")) {
            ifstream f("index.html", ios::binary); stringstream ss; ss << f.rdbuf();
            if (!f) sendAll(c, httpResp(404, "text/plain", "index.html not found. Put it in the same folder as the server."));
            else sendAll(c, httpResp(200, "text/html; charset=utf-8", ss.str()));
        }
        else if (method == "POST" && path.rfind("/api/", 0) == 0)
            sendAll(c, httpResp(200, "application/json", atm.handle(path, body)));
        else sendAll(c, httpResp(404, "text/plain", "Not found"));
        closesocket(c);
    }
}