# source: https://github.com/yurochka/dsptools

# pylint: skip-file

import numpy as np


def invfreqz(h, w, nb, na, wt=None, gauss=False, real=True, maxiter=30, tol=0.01):
    """
Parameters:
h: Frequency response array
w: Normalized frequency array (from zero to pi)
nb: numerator order
na: denominator order
wt: weight array, same length with h
gauss: whether to use Gauss-Newton method, default False
real: whether real or complex filter, default True
maxiter: maximum number of iteration when using Gauss-Newton method, default 30
tol: tolerance when using Gauss-Newton method, default 0.01
"""
    if len(h) != len(w):
        raise ValueError('H and W should be of equal length.')
    nb = nb + 1
    nm = max(nb - 1, na)
    OM_a = np.mat(np.arange(0, nm + 1))
    OM_m = OM_a.T * np.mat(w)
    OM = np.exp(-1j * OM_m)
    Dva_a = np.transpose(OM[1:(na + 1)])
    h_t = np.transpose(np.mat(h))
    Dva_b = h_t * np.mat(np.ones(na))
    Dva = np.multiply(Dva_a, Dva_b)
    Dvb = -np.transpose(OM[0:nb])
    D_a = np.hstack((Dva, Dvb))
    if wt is None:
        wf = np.transpose(np.mat(np.ones_like(h)))
    else:
        wf = np.sqrt(np.transpose(np.mat(wt)))
    D_b = wf * np.mat(np.ones((1, na + nb)))
    D = np.multiply(D_a, D_b)
    if real:
        R = np.real(D.H * D)
        Vd = np.real(D.H * np.multiply(-h_t, wf))
    else:
        R = D.H * D
        Vd = D.H * np.multiply(-h_t, wf)
    th = R.I * Vd
    tht = th.T.getA()
    a = np.append([1], tht[0][0:na])
    b = tht[0][na:(na + nb)]
    if not gauss:
        return b, a
    else:
        indb = np.arange(len(b))
        indg = np.arange(len(a))
        a = polystab(a)
        GC_b = np.mat(b) * OM[indb, :]
        GC_a = np.mat(a) * OM[indg, :]
        GC = np.transpose(GC_b / GC_a)
        e = np.multiply(GC - h_t, wf)
        Vcap = e.H * e
        t = np.mat(np.append(a[1:(na + 1)], b[0:nb])).T
        gndir = 2 * tol + 1
        l = 0
        st = 0
        while (np.linalg.norm(gndir) > tol) and (l < maxiter) and (st != 1):
            l = l + 1
            D31_a = np.transpose(OM[1:(na + 1), :])
            D31_b = - GC / np.transpose(a * OM[0:(na + 1), :])
            D31_c = np.mat(np.ones((1, na)))
            D31 = np.multiply(D31_a, D31_b * D31_c)
            D32_a = np.transpose(OM[0:nb, :])
            D32_b = np.transpose(a * OM[0:(na + 1), :])
            D32_c = np.mat(np.ones((1, nb)))
            D32 = D32_a / (D32_b * D32_c)
            D3_a = np.hstack((D31, D32))
            D3_b = wf * np.mat(np.ones((1, na + nb)))
            D3 = np.multiply(D3_a, D3_b)
            e = np.multiply(GC - h_t, wf)
            if real:
                R = np.real(D3.H * D3)
                Vd = np.real(D3.H * e)
            else:
                R = D3.H * D3
                Vd = D3.H * e
            gndir = R.I * Vd
            ll = 0
            k = 1
            V1 = np.mat(Vcap + 1)
            while (V1[0][0] > Vcap) and (ll < 20):
                t1 = t - k * gndir
                if ll == 19:
                    t1 = t
                t1_v = np.transpose(t1).getA()[0]
                a = polystab(np.append([1], t1_v[0:na]))
                t1_v[0:na] = a[1:(na + 1)]
                b = t1_v[na:(na + nb)]
                GC_b = np.mat(b) * OM[indb, :]
                GC_a = np.mat(a) * OM[indg, :]
                GC = np.transpose(GC_b / GC_a)
                V1_a = np.multiply(GC - h_t, wf)
                V1 = V1_a.H * V1_a
                t1 = np.mat(np.append(a[1:(na + 1)], b[0:nb])).T
                k = k / 2
                ll = ll + 1
                if ll == 20:
                    st = 1
                if ll == 10:
                    gndir = Vd / np.linalg.norm(R) * R.shape[0]
                    k = 1
            t = t1
            Vcap = V1[0][0]
        return b, a


def polystab(a):
    if len(a) <= 1:
        return a
    else:
        v = np.roots(a)
        for i in range(len(v)):
            if v[i] != 0:
                vs = 0.5 * (np.sign(abs(v[i]) - 1) + 1)
                v[i] = (1 - vs) * v[i] + vs / v[i].conj()
        ind = np.nonzero(v)
        b = a[ind[0][0]] * np.poly(v)
        if np.iscomplex(b).any():
            b = np.real(b)
        return b
