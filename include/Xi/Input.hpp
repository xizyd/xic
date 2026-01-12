#ifndef XI_INPUT_H
#define XI_INPUT_H

#include <Xi/String.hpp>

namespace Xi {
    enum class InputType { Key, MouseBtn, MouseAxis };
    
    struct InputControl {
        String name;
        InputType type;
        i32 code;      
        bool active = true;
        
        bool down;     
        bool up;       
        bool held;     
        f32 value;     
    };
}
#endif