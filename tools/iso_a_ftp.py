# -*- coding: utf-8 -*-
# iso_a_ftp.py ("ISO to FTP"): a developer's one-time copy of an Xbox 360 ISO's files straight to the console's FTP
# server, without extracting them to disk first (the dev phone has little room, and 7 GB written twice is slow).
# Players do not need it: the NRO unpacks an .iso placed next to it on first start (rex/filesystem/first_launch.h).
#
#   python tools/iso_a_ftp.py <iso> <ip> <remote folder> [--puerto 5000] [--sin PATTERN]...
#   python tools/iso_a_ftp.py "NFS (Spain).iso" 10.241.80.168 /switch/nfsmw-nx-es/game_root
#
# Resumable: a remote file that already has the right size is skipped, so after an interruption run it again.
# Point it at a folder only this tool fills: a same-size file from another edition would be skipped too.
# Anonymous login, as the console's FTP servers (ftpd/sys-ftpd/Sphaira) use. --sin skips paths matching a glob
# (case-insensitive), e.g. --sin "\$SystemUpdate/*".
import argparse, fnmatch, ftplib, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fase1_extraer import SECTOR, detectar_base, leer_descriptor, recorrer  # noqa: E402


class Trozo:
    """File-like view of one ISO entry, for ftplib.storbinary."""

    def __init__(self, fh, inicio, tam):
        self.fh, self.pos, self.fin = fh, inicio, inicio + tam

    def read(self, n=-1):
        if self.pos >= self.fin:
            return b""
        n = self.fin - self.pos if n < 0 else min(n, self.fin - self.pos)
        self.fh.seek(self.pos)
        datos = self.fh.read(n)
        self.pos += len(datos)
        return datos


def conectar(ip, puerto):
    f = ftplib.FTP()
    f.connect(ip, puerto, timeout=120)
    f.login("anonymous", "anonymous")
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("ip")
    ap.add_argument("destino")
    ap.add_argument("--puerto", type=int, default=5000)
    ap.add_argument("--sin", action="append", default=[])
    a = ap.parse_args()
    destino = "/" + a.destino.strip("/")

    with open(a.iso, "rb") as fh:
        base, _ = detectar_base(fh)
        sector, tam = leer_descriptor(fh, base)
        entradas = [(r, e) for r, e in recorrer(fh, base, sector, tam)
                    if not any(fnmatch.fnmatch(r.lower(), p.lower()) for p in a.sin)]
        ficheros = [(r, e) for r, e in entradas if not e["dir"]]
        total = sum(e["tam"] for _, e in ficheros)
        print("%d ficheros, %.2f GB -> %s:%d%s" % (len(ficheros), total / 1e9, a.ip, a.puerto, destino), flush=True)

        f = conectar(a.ip, a.puerto)
        carpetas = sorted({destino} | {destino + "/" + os.path.dirname(r) for r, _ in ficheros if "/" in r},
                          key=len)
        for c in carpetas:
            ruta = ""
            for p in c.strip("/").split("/"):
                ruta += "/" + p
                try:
                    f.mkd(ruta)
                except ftplib.error_perm:
                    pass  # already there

        hecho, inicio = 0, time.time()
        for r, e in ficheros:
            remoto = destino + "/" + r
            for intento in range(5):
                try:
                    try:
                        if f.size(remoto) == e["tam"]:
                            print("  = %s" % r, flush=True)
                            break
                    except ftplib.error_perm:
                        pass  # not there yet
                    t0 = time.time()
                    f.storbinary("STOR " + remoto, Trozo(fh, base + e["sector"] * SECTOR, e["tam"]), blocksize=1 << 20)
                    if f.size(remoto) != e["tam"]:
                        raise IOError("tamano remoto distinto tras subir %s" % r)
                    dt = max(time.time() - t0, 1e-3)
                    print("  + %s  %.1f MB  %.1f MB/s" % (r, e["tam"] / 1e6, e["tam"] / 1e6 / dt), flush=True)
                    break
                except (OSError, EOFError, ftplib.error_temp, ftplib.error_reply) as ex:
                    print("  ! %s: %s (reintento %d)" % (r, ex, intento + 1), flush=True)
                    time.sleep(3)
                    try:
                        f = conectar(a.ip, a.puerto)
                    except OSError:
                        pass
            else:
                raise SystemExit("no se pudo subir %s" % r)
            hecho += e["tam"]
            el = time.time() - inicio
            print("  [%5.1f%%] %.2f/%.2f GB, %.0f s" % (100 * hecho / total, hecho / 1e9, total / 1e9, el), flush=True)
        f.quit()
        print("hecho")


if __name__ == "__main__":
    main()
