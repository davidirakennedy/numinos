// Pentadiagonal — solves A x = b when A is symmetric, positive definite and banded: every row
// touches only its two neighbours on each side. That's the shape you get whenever you ask for a
// curve that stays close to some targets while penalising its slope and its curvature (each
// penalty links a point to its neighbours), so smoothing problems like Honey's gain path are
// solved exactly, in a handful of operations per point, with no iteration.
#pragma once

#include <array>
#include <utility>

namespace numinos::dsp
{

template <int Capacity>
class Pentadiagonal
{
  public:
    // Start over with n unknowns, every entry zero.
    void clear(int n)
    {
        size = n;
        for (int i = 0; i < n; ++i)
            diag[i] = off1[i] = off2[i] = rhs[i] = 0.0;
    }

    // Add to entry (i, j) of A, and so to (j, i): A stays symmetric. |i - j| must be <= 2.
    void addMatrix(int i, int j, double v)
    {
        if (i > j)
            std::swap(i, j);
        if (j == i)
            diag[i] += v;
        else if (j == i + 1)
            off1[i] += v;
        else
            off2[i] += v;
    }

    void addRhs(int i, double v) { rhs[i] += v; }

    // Solve, writing x[0..n). A = L D L^T, with L unit lower-triangular and banded like A:
    //   l1[i] = L(i, i-1), l2[i] = L(i, i-2), d[i] = D(i, i).
    // Then forward substitution through L, a divide by D, and back substitution through L^T.
    void solve(double* x)
    {
        for (int i = 0; i < size; ++i)
        {
            l2[i] = i >= 2 ? off2[i - 2] / d[i - 2] : 0.0;
            l1[i] = i >= 1 ? (off1[i - 1] - (i >= 2 ? l2[i] * d[i - 2] * l1[i - 1] : 0.0)) / d[i - 1] : 0.0;
            d[i] = diag[i] - (i >= 1 ? l1[i] * l1[i] * d[i - 1] : 0.0) - (i >= 2 ? l2[i] * l2[i] * d[i - 2] : 0.0);
        }
        for (int i = 0; i < size; ++i)
            x[i] = rhs[i] - (i >= 1 ? l1[i] * x[i - 1] : 0.0) - (i >= 2 ? l2[i] * x[i - 2] : 0.0);
        for (int i = 0; i < size; ++i)
            x[i] /= d[i];
        for (int i = size - 1; i >= 0; --i)
            x[i] -= (i + 1 < size ? l1[i + 1] * x[i + 1] : 0.0) + (i + 2 < size ? l2[i + 2] * x[i + 2] : 0.0);
    }

  private:
    int size = 0;
    std::array<double, Capacity> diag{}, off1{}, off2{}, rhs{}; // A's diagonals (off1[i] = A(i, i+1)) and b
    std::array<double, Capacity> d{}, l1{}, l2{};               // the factorisation
};

} // namespace numinos::dsp
