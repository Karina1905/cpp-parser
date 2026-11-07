int main() {
    int a = 2 + 3 * 4;
    int b = (2 + 3) * 4;
    int c = -a + b / 2 % 3;
    double d = 1.5 * (a - b) / 4;
    cout << a << " " << b << " " << c << " " << d << endl;
    cout << (a < b) << " " << (a == 14 && !(b < 10)) << endl;
    return 0;
}
