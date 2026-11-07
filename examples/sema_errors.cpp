#include <iostream>
using namespace std;

class Point {
public:
    int x, y;
    Point(int a, int b) : x(a), y(b) {}
    int sum() { return x + y; }
};

int twice(int v) { return v * 2; }

int noret(int v) {
    if (v > 0) return 1;
}

void nothing() { return 5; }

int main() {
    int a = 1;
    int a = 2;
    cout << b << endl;
    string s = "hi";
    int n = s;
    int m = s + 1;
    Point p(1);
    Point q(1, 2);
    cout << q.z << endl;
    q.area();
    twice(1, 2);
    unknownFunc(3);
    break;
    5 = a;
    a = this->x;
    double d = 7.5 % 2;
    return 0;
}
