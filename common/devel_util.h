
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

    template<class I, class T>
    friend T& operator<<(T& out, const IntWrapper<I, ambg>& x)
    {
        out << x.v;
        return out;
    }

    bool operator<(const IntWrapper& w) const
    {
        return v < w.v;
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


//typedef IntWrapper<int32_t, 0> SRTSOCKET;
//typedef IntWrapper<int, 1> SRTSTATUS;
//typedef IntWrapperLoose<int, 1> SRTSTATUS_LOOSE;

