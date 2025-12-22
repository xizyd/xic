// src/Xi/Time.hpp

#ifndef XI_TIME_HPP
#define XI_TIME_HPP 1

#include "String.hpp"
#include "Utils.hpp"

namespace Xi {

class Time {
private:
    // -------------------------------------------------------------------------
    // Constants & Algorithms
    // -------------------------------------------------------------------------
    static constexpr u64 US_PER_SEC   = 1000000ULL;
    static constexpr u64 US_PER_MIN   = 60000000ULL;
    static constexpr u64 US_PER_HOUR  = 3600000000ULL;
    static constexpr u64 US_PER_DAY   = 86400000000ULL;
    static constexpr int DAYS_PER_ERA      = 146097; // 400 years

    // Helper: Is Leap Year
    static bool is_leap(int y) {
        return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
    }

    // Helper: Days in specific month
    static int days_in_month(int m, int y) {
        static const u8 days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (m == 2 && is_leap(y)) return 29;
        return days[m-1];
    }

    // High-performance Civil Time Algorithm (Epoch -> YMD)
    // Returns year, month, day, dayInYear
    static void civ_from_days(long long z, int& y, int& m, int& d, int& doy) {
        z += 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        y = static_cast<int>(yoe) + era * 400;
        doy = doe - (365*yoe + yoe/4 - yoe/100);
        const unsigned mp = (5*doy + 2)/153;
        d = doy - (153*mp + 2)/5 + 1;
        m = mp + (mp < 10 ? 3 : -9);
        y += (m <= 2);
    }

    // High-performance Inverse (YMD -> Epoch Days)
    static long long days_from_civ(int y, int m, int d) {
        y -= (m <= 2);
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153*(m + (m > 2 ? -3 : 9)) + 2)/5 + d - 1;
        const unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
        return era * 146097 + static_cast<long long>(doe) - 719468;
    }

    // Parsing Helper
    static int parse_int(const char*& str, int len) {
        int v = 0;
        for(int i=0; i<len; ++i) {
            char c = *str;
            if (c >= '0' && c <= '9') {
                v = v * 10 + (c - '0');
                str++;
            } else {
                break; // Stop if non-digit hit early
            }
        }
        return v;
    }

    // Case-insensitive char compare
    static bool ch_eq(char a, char b) {
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        return a == b;
    }

public:
    // -------------------------------------------------------------------------
    // The Source of Truth
    // -------------------------------------------------------------------------
    u64 us; // Microseconds since Epoch (1970-01-01 00:00:00 UTC)

    // -------------------------------------------------------------------------
    // Constructors
    // -------------------------------------------------------------------------
    
    // Default: Now
    Time() {
        // Assume epochMicros() returns time since boot/absolute. 
        // For a real server, you'd seed this with system epoch.
        // For this implementation, we take epochMicros() as-is.
        us = epochMicros(); 
    }

    Time(u64 u) : us(u) {}

    // Parsing Constructor
    Time(const Xi::String& date, const Xi::String& fmt) : us(0) {
        const char* s = date.c_str();
        const char* f = fmt.c_str();

        int Y=1970, M=1, D=1, h=0, m=0, sc=0;
        bool isPM = false;
        int tzOffsetHours = 0;
        int tzOffsetMins = 0;

        while (*f) {
            if (*f == 'y' && *(f+1) == 'y' && *(f+2) == 'y' && *(f+3) == 'y') {
                Y = parse_int(s, 4); f += 4;
            } 
            else if (*f == 'm' && *(f+1) == 'm') {
                // Heuristic: If we are after 'h' (hour), it's minute, else month
                // This is a simple greedy parser.
                // Better approach: Check strict context.
                // Assuming standard ordering YMD HMS, first mm is month.
                // We'll trust the user provided format order.
                // Let's look ahead in format for 's' to guess minute, or back for 'h'
                bool isMinute = false;
                const char* back = f;
                while(back > fmt.c_str()) {
                    if (*back == 'h') { isMinute = true; break; }
                    back--;
                }
                
                int val = parse_int(s, 2);
                if (isMinute) m = val; else M = val;
                f += 2;
            }
            else if (*f == 'd' && *(f+1) == 'd') {
                D = parse_int(s, 2); f += 2;
            }
            else if (*f == 'h' && *(f+1) == 'h') {
                h = parse_int(s, 2); f += 2;
            }
            else if (*f == 's' && *(f+1) == 's') {
                sc = parse_int(s, 2); f += 2;
            }
            else if (*f == 'r' && *(f+1) == 'r') { // AM/PM
                // Scan next 2 chars of s
                if (ch_eq(*s, 'P') && ch_eq(*(s+1), 'M')) isPM = true;
                s += 2; f += 2;
            }
            else if (*f == 'z' && *(f+1) == 'z') { // Timezone +05 or +05:30
                // Expect + or -
                int sign = 1;
                if (*s == '-') sign = -1;
                else if (*s == '+') sign = 1;
                else break; // Error
                s++;
                
                tzOffsetHours = parse_int(s, 2);
                if (*s == ':') s++;
                // If more digits exist, assume minutes
                if (*s >= '0' && *s <= '9') tzOffsetMins = parse_int(s, 2);
                
                tzOffsetHours *= sign;
                tzOffsetMins *= sign;
                f += 2;
            }
            else {
                // Skip delimiters
                if (*s == *f) { s++; } 
                f++;
            }
        }

        // Logic adjustments
        if (isPM && h < 12) h += 12;
        if (!isPM && h == 12) h = 0; // 12 AM is 00

        // Calculate Epoch
        long long totalDays = days_from_civ(Y, M, D);
        long long totalSecs = totalDays * 86400 + h * 3600 + m * 60 + sc;
        
        // Apply TZ offset (inverse logic: if input says +02:00, UTC is -2 hours)
        totalSecs -= (tzOffsetHours * 3600 + tzOffsetMins * 60);

        us = totalSecs * US_PER_SEC;
    }

    // -------------------------------------------------------------------------
    // Properties (Proxies for Syntax Sugar)
    // allows t.year = 2025; and int y = t.year;
    // -------------------------------------------------------------------------
    template<typename Owner, int (Owner::*Getter)() const, void (Owner::*Setter)(int)>
    struct Property {
        Owner* self;
        Property(Owner* s) : self(s) {}
        operator int() const { return (self->*Getter)(); }
        Property& operator=(int v) { (self->*Setter)(v); return *this; }
        Property& operator+=(int v) { (self->*Setter)((self->*Getter)() + v); return *this; }
        Property& operator-=(int v) { (self->*Setter)((self->*Getter)() - v); return *this; }
    };

    // -------------------------------------------------------------------------
    // Getters & Setters Implementation
    // -------------------------------------------------------------------------

    // --- Microseconds ---
    int getUsPart() const { return us % US_PER_SEC; }
    void setUsPart(int v) { us = (us / US_PER_SEC) * US_PER_SEC + v; }

    // --- Second ---
    int getSecond() const { return (us / US_PER_SEC); } // Total seconds (overflows int likely)
    int getSecondInMinute() const { return (us / US_PER_SEC) % 60; }
    void setSecondInMinute(int v) {
        long long totalSec = us / US_PER_SEC;
        long long baseMin = totalSec / 60;
        us = (baseMin * 60 + v) * US_PER_SEC + (us % US_PER_SEC);
    }

    // --- Minute ---
    int getMinute() const { return (us / US_PER_MIN); } 
    int getMinuteInHour() const { return (us / US_PER_MIN) % 60; }
    void setMinuteInHour(int v) {
        long long totalMin = us / US_PER_MIN;
        long long baseHour = totalMin / 60;
        long long secPart = (us / US_PER_SEC) % 60;
        long long usPart = us % US_PER_SEC;
        us = ((baseHour * 60 + v) * 60 + secPart) * US_PER_SEC + usPart;
    }

    // --- Hour ---
    int getHourInDay() const { return (us / US_PER_HOUR) % 24; }
    void setHourInDay(int v) {
        long long days = us / US_PER_DAY;
        long long oldSecsOfDay = (us % US_PER_DAY) / US_PER_SEC;
        int oldH = oldSecsOfDay / 3600;
        int rem = oldSecsOfDay % 3600;
        us = (days * 86400 + v * 3600 + rem) * US_PER_SEC + (us % US_PER_SEC);
    }

    // --- Date Components (Heavy Lifting) ---
    void getDate(int& y, int& m, int& d, int& doy) const {
        long long days = us / US_PER_DAY;
        civ_from_days(days, y, m, d, doy);
    }

    int getYear() const { int y,m,d,dy; getDate(y,m,d,dy); return y; }
    int getMonth() const { int y,m,d,dy; getDate(y,m,d,dy); return m; }
    int getDay() const { int y,m,d,dy; getDate(y,m,d,dy); return d; } // Day of month
    int getDayInYear() const { int y,m,d,dy; getDate(y,m,d,dy); return dy; } // 0-365
    int getMonthInYear() const { return getMonth(); } 
    int getDayInMonth() const { return getDay(); }

    void setYear(int v) {
        int y,m,d,dy; getDate(y,m,d,dy);
        // Clamp day if leap year changes
        int maxD = days_in_month(m, v);
        if (d > maxD) d = maxD;
        updateDate(v, m, d);
    }

    void setMonth(int v) {
        int y,m,d,dy; getDate(y,m,d,dy);
        int maxD = days_in_month(v, y);
        if (d > maxD) d = maxD;
        updateDate(y, v, d);
    }

    void setDay(int v) {
        int y,m,d,dy; getDate(y,m,d,dy);
        updateDate(y, m, v);
    }

    // Helper to reconstruct US from YMD + existing time
    void updateDate(int y, int m, int d) {
        long long days = days_from_civ(y, m, d);
        long long timePart = us % US_PER_DAY;
        us = days * US_PER_DAY + timePart;
    }

public:
    // -------------------------------------------------------------------------
    // Public Properties (Bound to Getters/Setters)
    // -------------------------------------------------------------------------
    
    // "t.year" syntax support
    Property<Time, &Time::getYear, &Time::setYear> year{this};
    Property<Time, &Time::getMonth, &Time::setMonth> month{this};
    Property<Time, &Time::getMonth, &Time::setMonth> monthInYear{this};
    Property<Time, &Time::getDay, &Time::setDay> day{this};
    Property<Time, &Time::getDay, &Time::setDay> dayInMonth{this};

    int dayInYear() const { return getDayInYear(); }

    Property<Time, &Time::getHourInDay, &Time::setHourInDay> hour{this}; // 0-23
    Property<Time, &Time::getMinuteInHour, &Time::setMinuteInHour> minute{this};
    Property<Time, &Time::getMinuteInHour, &Time::setMinuteInHour> minuteInHour{this};
    Property<Time, &Time::getSecondInMinute, &Time::setSecondInMinute> second{this};
    Property<Time, &Time::getSecondInMinute, &Time::setSecondInMinute> secondInMinute{this};

    // -------------------------------------------------------------------------
    // Formatting
    // -------------------------------------------------------------------------

    Xi::String toString(const Xi::String& fmt = "yyyy/mm/dd hh:mm:ss", int targetTzHours = 0) const {
        // Apply Timezone Offset temporarily
        long long localUs = us + (targetTzHours * 3600 * US_PER_SEC);
        
        int y, m, d, doy;
        long long days = localUs / US_PER_DAY;
        civ_from_days(days, y, m, d, doy);

        long long timeOfDay = localUs % US_PER_DAY;
        int h = (timeOfDay / US_PER_HOUR);
        int mn = (timeOfDay % US_PER_HOUR) / US_PER_MIN;
        int s = (timeOfDay % US_PER_MIN) / US_PER_SEC;

        Xi::String res;
        const char* f = fmt.c_str();
        
        while(*f) {
            if (*f == 'y' && *(f+1) == 'y' && *(f+2) == 'y' && *(f+3) == 'y') {
                res += y; f+=4;
            }
            else if (*f == 'm' && *(f+1) == 'm') {
                bool isMin = false;
                const char* back = f;
                while(back > fmt.c_str()) { if(*back=='h'){isMin=true; break;} back--; }
                
                int v = isMin ? mn : m;
                if(v < 10) res += '0'; res += v;
                f+=2;
            }
            else if (*f == 'd' && *(f+1) == 'd') {
                if(d < 10) res += '0'; res += d; f+=2;
            }
            else if (*f == 'h' && *(f+1) == 'h') {
                if(h < 10) res += '0'; res += h; f+=2;
            }
            else if (*f == ':' && *(f+1) == 'm' && *(f+2) == 'm') { // :mm usually minutes
                res += ':';
                if(mn < 10) res += '0'; res += mn;
                f+=3;
            }
            else if (*f == 's' && *(f+1) == 's') {
                if(s < 10) res += '0'; res += s; f+=2;
            }
            else if (*f == 'r' && *(f+1) == 'r') {
                res += (h >= 12) ? "PM" : "AM";
                f+=2;
            }
            else if (*f == 'z' && *(f+1) == 'z') {
                if (targetTzHours >= 0) res += '+'; else res += '-';
                int absH = targetTzHours < 0 ? -targetTzHours : targetTzHours;
                if(absH < 10) res += '0'; res += absH;
                res += ":00";
                f+=2;
            }
            else {
                res += *f; f++;
            }
        }
        return res;
    }
};

} // namespace Xi

#endif // XI_TIME_HPP