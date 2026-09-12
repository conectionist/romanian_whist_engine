#include <romanian_whist/strategies/common/Hypergeometric.h>

namespace romanian_whist::common
{

namespace
{

struct Hyper0Table
{
    // n <= 48, k <= 16, h <= 8
    float table[49][17][9]{};

    constexpr Hyper0Table()
    {
        for(unsigned int n = 0; n <= 48; ++n)
        {
            for(unsigned int k = 0; k <= 16; ++k)
            {
                for(unsigned int h = 0; h <= 8; ++h)
                {
                    if(h == 0)
                    {
                        table[n][k][h] = 1.0f;
                    }
                    else if(n < h || k > n || n - k < h)
                    {
                        table[n][k][h] = 0.0f;
                    }
                    else
                    {
                        double p = 1.0;
                        for(unsigned int i = 0; i < h; ++i)
                        {
                            p *= static_cast<double>(n - k - i) / static_cast<double>(n - i);
                        }
                        table[n][k][h] = static_cast<float>(p);
                    }
                }
            }
        }
    }
};

inline constexpr Hyper0Table HYPER0_TABLE{};

} // namespace

float hyper0(unsigned int k, unsigned int n, unsigned int h)
{
    if(h == 0)
        return 1.0f;
    if(n < h || k > n || n - k < h)
        return 0.0f;
    if(n <= 48 && k <= 16 && h <= 8)
    {
        return HYPER0_TABLE.table[n][k][h];
    }
    // Fallback if ever called with larger bounds (defensive)
    double p = 1.0;
    for(unsigned int i = 0; i < h; ++i)
    {
        p *= static_cast<double>(n - k - i) / static_cast<double>(n - i);
    }
    return static_cast<float>(p);
}

} // namespace romanian_whist::common
