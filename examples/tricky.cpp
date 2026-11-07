#include <iostream>
using namespace std;

class P {
public:
    int x;
    P() { x = 7; }
    P(int v) : x(v) {}
};

void mod(P p) { p.x = 100; }

int g = 0;
int next() { g++; return g; }

double half(int a) { return a / 2; }

int depth(int n) { return n == 0 ? 0 : 1 + depth(n - 1); }

int main() {
    int a = 1, b = 0;
    if (a) if (b) cout << "A"; else cout << "B";
    cout << endl;

    cout << 2 - 3 - 4 << " " << 2 * 3 + 4 * 5 << " " << -2 * 3 << " " << 10 - 2 * 3 % 4 << endl;

    int c = 3;
    a = b = c;
    int i = 5;
    int j = i++ + ++i;
    int arr[3] = {1, 2, 3};
    int k = 0;
    arr[k++] = 10;
    cout << a << b << " " << i << " " << j << " " << arr[0] << k << endl;

    int x = 2;
    { int x = 3; cout << x; }
    for (int q = 0; q < 2; q++) { int x = q; cout << x; }
    cout << x << endl;

    int t = 7.9;
    double d = 7;
    bool ok = 5;
    cout << t << " " << d << " " << half(7) << " " << ok << " " << 7 / 2 << " " << -7 % 3 << endl;

    P ps[3];
    ps[1].x = 9;
    P one(1);
    P two = one;
    two.x = 2;
    mod(one);
    cout << ps[0].x << ps[1].x << " " << one.x << two.x << endl;

    P f();
    int s = next() + next();
    cout << s << g << " " << depth(20000) << endl;

    int m = 0;
    do { m++; if (m == 2) continue; cout << m; } while (m < 4);
    cout << endl;

    char ch = 'A';
    int code = ch;
    ch++;
    cout << ch << " " << code << " " << ch + 1 << " " << (char)(ch + 1) << " " << ch - 'A' << endl;

    int idx = 0;
    cout << arr[idx++]-- << arr[0] << idx << " " << ++arr[2] << " " << (arr[1] = 9) << " " << -idx++ << idx << endl;
    return 0;
}
