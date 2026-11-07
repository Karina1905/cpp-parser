#include <iostream>
#include <string>
using namespace std;

class Account {
private:
    string owner;
    double balance;
    int ops;
public:
    Account() : owner("nobody"), balance(0), ops(0) {}
    Account(string o, double b) { owner = o; balance = b; ops = 0; }

    bool withdraw(double amount) {
        if (amount <= 0) return false;
        else if (amount > this->balance) {
            cout << "  [" << owner << "] insufficient funds" << endl;
            return false;
        } else {
            balance -= amount;
            ops++;
            return true;
        }
    }
    void deposit(double amount) { balance += amount; ops = ops + 1; }
    double getBalance() { return balance; }
    string getOwner() { return owner; }
    int getOps() { return ops; }
};

double total(double values[], int n) {
    double s = 0;
    for (int i = 0; i < n; ++i) s = s + values[i];
    return s;
}

int main() {
    Account a("Alice", 100);
    Account b;
    a.withdraw(30);
    a.withdraw(500);
    b.deposit(42.5);
    cout << a.getOwner() << ": " << a.getBalance() << " (" << a.getOps() << " ops)" << endl;
    cout << b.getOwner() << ": " << b.getBalance() << endl;

    double v[3] = {1.5, 2.5, 3};
    cout << "total = " << total(v, 3) << endl;

    int n = 0, k;
    cin >> k;
    do {
        n += k;
        k--;
    } while (k > 0);
    cout << "sum 1..k = " << n << endl;

    string s = "abc";
    s = s + "def";
    cout << s << " has " << s.length() << " chars" << endl;
    char c = 'Z';
    cout << "char: " << c << ", (int) 7.9 = " << (int) 7.9 << ", 2^10 = " << pow(2, 10) << endl;
    return 0;
}
