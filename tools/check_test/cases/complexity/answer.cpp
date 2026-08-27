#include "answer.hpp"

int answer() {
    int result = 0;
    for(int bit = 0; bit < 12; ++bit) {
        if(bit == 0) result += 1;
        if(bit == 1) result += 1;
        if(bit == 2) result += 1;
        if(bit == 3) result += 1;
        if(bit == 4) result += 1;
        if(bit == 5) result += 1;
        if(bit == 6) result += 1;
        if(bit == 7) result += 1;
        if(bit == 8) result += 1;
        if(bit == 9) result += 1;
        if(bit == 10) result += 1;
        if(bit == 11) result += 31;
    }
    return result;
}
