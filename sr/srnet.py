"""Real-ESRGAN SRVGGNetCompact (realesr-animevideov3) on CPU via onnxruntime, no torch needed."""
import zipfile, pickle, struct, sys, os
import numpy as np

def load_pth(path):
    z = zipfile.ZipFile(path)
    pre = z.namelist()[0].split('/')[0]
    class Storage:
        def __init__(s, key): s.key = key
    def rebuild(storage, offset, size, stride, *a):
        raw = np.frombuffer(z.read(f'{pre}/data/{storage.key}'), np.float32)
        n = int(np.prod(size)) if size else 1
        return raw[offset:offset + n].reshape(size).copy()
    class U(pickle.Unpickler):
        def find_class(s, mod, name):
            if name == '_rebuild_tensor_v2': return rebuild
            if mod == 'collections' and name == 'OrderedDict':
                import collections; return collections.OrderedDict
            if mod.startswith('torch'): return lambda *a, **k: None
            raise pickle.UnpicklingError(f'{mod}.{name}')
        def persistent_load(s, pid):
            return Storage(pid[2])
    d = U(z.open(f'{pre}/data.pkl')).load()
    return d.get('params', d.get('params_ema', d))

# ---- minimal protobuf / ONNX writer
def varint(v):
    out = bytearray()
    v &= (1 << 64) - 1
    while True:
        b = v & 0x7F; v >>= 7
        if v: out.append(b | 0x80)
        else: out.append(b); return bytes(out)
def fld(n, wt, payload):
    return varint(n << 3 | wt) + payload
def f_int(n, v): return fld(n, 0, varint(v))
def f_bytes(n, b):
    if isinstance(b, str): b = b.encode()
    return fld(n, 2, varint(len(b)) + b)
def f_float(n, v): return fld(n, 5, struct.pack('<f', v))

def tensor(name, arr):
    arr = np.ascontiguousarray(arr, np.float32)
    return b''.join(f_int(1, d) for d in arr.shape) + f_int(2, 1) + f_bytes(8, name) + f_bytes(9, arr.tobytes())
def attr_ints(name, vals): return f_bytes(1, name) + b''.join(f_int(8, v) for v in vals) + f_int(20, 7)
def attr_int(name, v): return f_bytes(1, name) + f_int(3, v) + f_int(20, 2)
def attr_str(name, s): return f_bytes(1, name) + f_bytes(4, s) + f_int(20, 3)
def node(op, ins, outs, attrs=()):
    return b''.join(f_bytes(1, i) for i in ins) + b''.join(f_bytes(2, o) for o in outs) + f_bytes(4, op) + b''.join(f_bytes(5, a) for a in attrs)
def vinfo(name, dims):
    shape = b''.join(f_bytes(1, (f_int(1, d) if isinstance(d, int) else f_bytes(2, d))) for d in dims)
    tt = f_int(1, 1) + f_bytes(2, shape)
    return f_bytes(1, name) + f_bytes(2, f_bytes(1, tt))

def build(params, scale):
    nodes, inits = [], []
    x = 'input'
    i = 0; li = 0
    keys = sorted({int(k.split('.')[1]) for k in params})
    for k in keys:
        w = params.get(f'body.{k}.weight'); b = params.get(f'body.{k}.bias')
        if b is not None:   # conv
            inits += [tensor(f'w{k}', w), tensor(f'b{k}', b)]
            nodes.append(node('Conv', [x, f'w{k}', f'b{k}'], [f'c{k}'], [attr_ints('pads', [1, 1, 1, 1]), attr_ints('kernel_shape', [3, 3])]))
            x = f'c{k}'
        else:               # PReLU
            inits.append(tensor(f'p{k}', w.reshape(-1, 1, 1)))
            nodes.append(node('PRelu', [x, f'p{k}'], [f'a{k}']))
            x = f'a{k}'
    nodes.append(node('DepthToSpace', [x], ['output'], [attr_int('blocksize', scale), attr_str('mode', 'CRD')]))
    g = b''.join(f_bytes(1, n) for n in nodes) + f_bytes(2, 'srvgg') + b''.join(f_bytes(5, t) for t in inits)
    g += f_bytes(11, vinfo('input', [1, 3, 'h', 'w'])) + f_bytes(12, vinfo('output', [1, 3, 'H', 'W']))
    m = f_int(1, 8) + f_bytes(8, f_bytes(1, '') + f_int(2, 17)) + f_bytes(2, 'srnet') + f_bytes(7, g)
    return m

class SR:
    def __init__(self, pth, scale=4, threads=0):
        import onnxruntime as ort
        p = load_pth(pth)
        o = ort.SessionOptions()
        if threads: o.intra_op_num_threads = threads
        self.s = ort.InferenceSession(build(p, scale), o, providers=['CPUExecutionProvider'])
        self.scale = scale
    def __call__(self, rgb):
        """rgb: HxWx3 uint8 -> (H*s)x(W*s)x3 float32 0..1"""
        x = rgb.astype(np.float32).transpose(2, 0, 1)[None] / 255.0
        y = self.s.run(None, {'input': x})[0][0]
        y += np.repeat(np.repeat(x[0], self.scale, 1), self.scale, 2)
        return np.clip(y.transpose(1, 2, 0), 0, 1)

if __name__ == '__main__':
    import time
    from PIL import Image
    sr = SR(sys.argv[1], 4)
    im = np.asarray(Image.open(sys.argv[2]).convert('RGB'))
    t = time.time(); y = sr(im); print('time', time.time() - t, y.shape)
    Image.fromarray((y * 255 + 0.5).astype(np.uint8)).save(sys.argv[3])


# ---- RRDBNet (ESRGAN / RealESRGAN_x4plus, x4plus_anime_6B)
def attr_float(name, v): return f_bytes(1, name) + f_float(2, v) + f_int(20, 1)

def build_rrdb(params):
    nodes, inits = [], []
    cnt = [0]
    def u(p='t'):
        cnt[0] += 1; return f'{p}{cnt[0]}'
    def conv(x, key):
        inits.append(tensor(key + '.w', params[key + '.weight'])); inits.append(tensor(key + '.b', params[key + '.bias']))
        y = u('c'); nodes.append(node('Conv', [x, key + '.w', key + '.b'], [y], [attr_ints('pads', [1, 1, 1, 1]), attr_ints('kernel_shape', [3, 3])])); return y
    def lrelu(x):
        y = u('l'); nodes.append(node('LeakyRelu', [x], [y], [attr_float('alpha', 0.2)])); return y
    def cat(xs):
        y = u('k'); nodes.append(node('Concat', xs, [y], [attr_int('axis', 1)])); return y
    def add(a, b):
        y = u('a'); nodes.append(node('Add', [a, b], [y])); return y
    inits.append(tensor('k02', np.array([0.2], np.float32)))
    inits.append(tensor('sc2', np.array([1, 1, 2, 2], np.float32)))
    def scale02(x):
        y = u('m'); nodes.append(node('Mul', [x, 'k02'], [y])); return y
    def up2(x):
        y = u('r'); nodes.append(node('Resize', [x, '', 'sc2'], [y], [attr_str('mode', 'nearest'),
                     attr_str('coordinate_transformation_mode', 'asymmetric'), attr_str('nearest_mode', 'floor')])); return y
    nb = 1 + max(int(k.split('.')[1]) for k in params if k.startswith('body.'))
    feat = conv('input', 'conv_first')
    x = feat
    for b in range(nb):
        inp = x
        for r in (1, 2, 3):
            pre = f'body.{b}.rdb{r}'
            x0 = x
            x1 = lrelu(conv(x0, pre + '.conv1'))
            x2 = lrelu(conv(cat([x0, x1]), pre + '.conv2'))
            x3 = lrelu(conv(cat([x0, x1, x2]), pre + '.conv3'))
            x4 = lrelu(conv(cat([x0, x1, x2, x3]), pre + '.conv4'))
            x5 = conv(cat([x0, x1, x2, x3, x4]), pre + '.conv5')
            x = add(scale02(x5), x0)
        x = add(scale02(x), inp)
    feat = add(feat, conv(x, 'conv_body'))
    feat = lrelu(conv(up2(feat), 'conv_up1'))
    feat = lrelu(conv(up2(feat), 'conv_up2'))
    out = conv(lrelu(conv(feat, 'conv_hr')), 'conv_last')
    nodes.append(node('Identity', [out], ['output']))
    g = b''.join(f_bytes(1, n) for n in nodes) + f_bytes(2, 'rrdb') + b''.join(f_bytes(5, t) for t in inits)
    g += f_bytes(11, vinfo('input', [1, 3, 'h', 'w'])) + f_bytes(12, vinfo('output', [1, 3, 'H', 'W']))
    return f_int(1, 8) + f_bytes(8, f_bytes(1, '') + f_int(2, 17)) + f_bytes(2, 'srnet') + f_bytes(7, g)

class RRDB:
    def __init__(self, pth, threads=0):
        import onnxruntime as ort
        o = ort.SessionOptions()
        if threads: o.intra_op_num_threads = threads
        self.s = ort.InferenceSession(build_rrdb(load_pth(pth)), o, providers=['CPUExecutionProvider'])
    def __call__(self, rgb):
        x = rgb.astype(np.float32).transpose(2, 0, 1)[None] / 255.0
        y = self.s.run(None, {'input': x})[0][0]
        return np.clip(y.transpose(1, 2, 0), 0, 1)
