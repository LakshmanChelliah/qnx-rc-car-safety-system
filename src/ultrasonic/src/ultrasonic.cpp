#include "../include/UltrasonicNode.hpp"

int main() {
    UltrasonicNode node;
    return node.start() ? 0 : 1;
}

