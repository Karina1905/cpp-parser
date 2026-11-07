#include <iostream>
#include <cmath>
using namespace std;

class Shape {
public:
    string name;
    Shape(string n) : name(n) {}
    double area() { return 0; }
    void describe() {
        cout << name << " with area " << area() << endl;
    }
};

class Rect : public Shape {
public:
    double w, h;
    Rect(double w0, double h0) : Shape("Rect"), w(w0), h(h0) {}
    double area() { return w * h; }
};

class Circle : public Shape {
public:
    double r;
    Circle(double r0) : Shape("Circle"), r(r0) {}
    double area();
};

double Circle::area() { return 3.14159 * r * r; }

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int counter = 0;

void tick() { counter++; }

int main() {
    Rect a(3, 4);
    Circle c(2.5);
    cout << "Rect area = " << a.area() << endl;
    cout << "Circle area = " << c.area() << endl;
    a.describe();

    int arr[5] = {5, 3, 8, 1, 9};
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        if (arr[i] % 2 == 0) continue;
        sum += arr[i];
    }
    cout << "Sum of odd = " << sum << endl;

    int i = 0;
    while (true) {
        i++;
        if (i > 3) break;
        tick();
    }
    cout << "counter = " << counter << ", 5! = " << factorial(5) << endl;
    cout << "7 / 2 = " << 7 / 2 << ", 7.0 / 2 = " << 7.0 / 2 << ", -7 % 3 = " << -7 % 3 << endl;
    bool ok = a.area() > c.area() && i == 4;
    cout << "ok = " << ok << ", max = " << (ok ? "rect" : "circle") << endl;
    return 0;
}
