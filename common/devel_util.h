
#include <ostream>

template <typename T, typename OS>
concept Streamable = requires(OS& os, T value) {
    { os << value };
};

template<class INT, int ambg>
struct IntWrapper
{
    INT v;

    IntWrapper() {}
    explicit IntWrapper(INT val): v(val) {}

    bool operator==(const IntWrapper& x) const
    {
        return v == x.v;
    }

    bool operator!=(const IntWrapper& x) const
    {
        return !(*this == x);
    }

    explicit operator INT() const
    {
        return v;
    }

    bool operator<(const IntWrapper& w) const
    {
        return v < w.v;
    }

    template<class Str>
    requires Streamable<Str, INT>
    friend Str& operator<<(Str& out, const IntWrapper<INT, ambg>& x)
    {
        out << x.v;
        return out;
    }

    friend std::ostream& operator<<(std::ostream& out, const IntWrapper<INT, ambg>& x)
    {
        out << x.v;
        return out;
    }
};

template<class INT, int ambg>
struct IntWrapperLoose: IntWrapper<INT, ambg>
{
    typedef IntWrapper<INT, ambg> base_t;
    explicit IntWrapperLoose(INT val): base_t(val) {}

    bool operator==(const IntWrapper<INT, ambg>& x) const
    {
        return this->v == x.v;
    }

    friend bool operator==(const IntWrapper<INT, ambg>& x, const IntWrapperLoose& y)
    {
        return x.v == y.v;
    }

    bool operator==(INT val) const
    {
        return this->v == val;
    }

    friend bool operator==(INT val, const IntWrapperLoose<INT, ambg>& x)
    {
        return val == x.v;
    }

    operator INT() const
    {
        return this->v;
    }
};


typedef IntWrapper<int32_t, 0> SRTSOCKET;
typedef IntWrapper<int, 1> SRTSTATUS;
typedef IntWrapperLoose<int, 1> SRTSTATUS_LOOSE;


