#include <iostream>
using namespace std;

class Stack {
private:
    int data[10];
    int top;
public:
    Stack() : top(0) {}
    void push(int v) { if (top < 10) data[top++] = v; }
    int pop() { return top > 0 ? data[--top] : -1; }
    bool empty() { return top == 0; }
    int size() { return top; }
};

int main() {
    Stack s;
    for (int i = 1; i <= 5; i++) s.push(i * i);
    cout << "size = " << s.size() << endl;
    while (!s.empty()) cout << s.pop() << " ";
    cout << endl << "pop from empty: " << s.pop() << endl;
    return 0;
}
