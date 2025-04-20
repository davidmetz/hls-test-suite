import collections
import dataclasses
import math
import pprint
import re
import shutil
import tempfile

import vcdlib
import subprocess
import pathlib
import multiprocessing


@dataclasses.dataclass
class RhlsBuf:
    depth: int
    width: int
    pass_through: bool
    id: int = 0

    def to_chisel(self):
        return f"emitVerilog(new RhlsBuf({self.depth}, {self.width}, {str(self.pass_through).lower()}))"

    @staticmethod
    def from_instance(buf: str):
        match = re.match(
            r"op_HLS_BUF_(?P<pass_through>P_)?(?P<depth>\d+)_IN1_W(?P<width>\d+)_OUT1_W(?P=width)_(?P<id>\d+)",
            buf).groupdict()
        pass_through = bool(match["pass_through"])
        depth = int(match["depth"])
        width = int(match["width"])
        id = int(match["id"])
        return RhlsBuf(depth, width, pass_through, id)

    def to_module(self):
        return f"op_HLS_BUF{'_P' if self.pass_through else ''}_{self.depth}_I{self.width}W_O{self.width}W"

    def to_instance(self):
        return f"op_HLS_BUF{'_P' if self.pass_through else ''}_{self.depth}_IN1_W{self.width}_OUT1_W{self.width}_{self.id}"


@dataclasses.dataclass
class RhlsDecLoad:
    depth: int
    width: int
    addr_width: int
    type_name: str
    id: int = 0

    def to_chisel(self):
        return f'emitVerilog(new RhlsDecLoad({self.depth}, {self.width}, {self.addr_width}, "{self.type_name}"))'

    @staticmethod
    def from_instance(buf: str):
        match = re.match(
            r"op_HLS_DEC_LOAD_(?P<depth>\d+)_(?P<type_name>.+)_IN2_W(?P<width>\d+)_OUT2_W(?P<addr_width>\d+)_(?P<id>\d+)",
            buf).groupdict()
        depth = int(match["depth"])
        width = int(match["width"])
        addr_width = int(match["addr_width"])
        type_name = match["type_name"]
        id = int(match["id"])
        return RhlsDecLoad(depth, width, addr_width, type_name, id)

    def to_module(self):
        # TODO: fix this in a nicer way:
        type_name = self.type_name
        if "fixedvector" in type_name:
            type_name += "_"
        return f"op_HLS_DEC_LOAD_{self.depth}_{type_name}_I{self.addr_width}W_I{self.width}W_O{self.width}W_O{self.addr_width}W"

    def to_instance(self):
        return f"op_HLS_DEC_LOAD_{self.depth}_{self.type_name}_IN2_W{self.width}_OUT2_W{self.addr_width}_{self.id}"


def find_name_path(search_name: str, hier):
    for name, h in hier.items():
        if name == search_name:
            return (name,)
        if path := find_name_path(search_name, h):
            return (name,) + path


def to_pow2(x):
    if x:
        return int(2 ** math.ceil(math.log2(x)))
    return 0


def find_op_names(hier, name_contains: str):
    res = []
    for name, h in hier.items():
        if name_contains in name and "ready" not in name and "valid" not in name and "data" not in name:
            res.append(name)
        res.extend(find_op_names(h, name_contains))
    return res


@dataclasses.dataclass
class RhlsAccelerator:
    verilog_path: pathlib.Path

    @property
    def vcd_path(self):
        return self.verilog_path.with_suffix(".vcd")

    @property
    def dot_path(self):
        return self.verilog_path.with_suffix(".dot")

    @property
    def cpp_path(self):
        return self.verilog_path.with_suffix(".harness.cpp")

    @property
    def cpp_axi_path(self):
        return self.verilog_path.with_suffix(".harness_axi.cpp")

    @property
    def object_path(self):
        return self.verilog_path.with_suffix(".o")

    @property
    def json_path(self):
        return self.verilog_path.with_suffix(".json")

    @property
    def sim_path(self):
        return self.verilog_path.with_suffix("")

    @property
    def log_path(self):
        return self.verilog_path.with_suffix(".log")

    def remove(self):
        self.verilog_path.unlink(True)
        self.vcd_path.unlink(True)
        self.dot_path.unlink(True)
        self.cpp_path.unlink(True)
        self.cpp_axi_path.unlink(True)
        self.object_path.unlink(True)
        self.json_path.unlink(True)
        self.sim_path.unlink(True)
        self.log_path.unlink(True)
        # make sure everything is cleaned up
        assert not list(self.verilog_path.parent.glob(self.verilog_path.with_suffix(".*").name))

    def build_sim(self, mem_latency: int = 100):
        print(f"building {self.sim_path}")
        HLS_TEST_ROOT = pathlib.Path("..").absolute()
        with tempfile.TemporaryDirectory() as tmpdir:
            out = subprocess.run(
                f"""verilator --trace -CFLAGS -DTRACE_SIGNALS --cc --build --exe -Wno-WIDTH -j 12 -y {HLS_TEST_ROOT}/verilog_ops/float -y {HLS_TEST_ROOT}/verilog_ops/float/dpi -y {HLS_TEST_ROOT}/verilog_ops/buffer -y {HLS_TEST_ROOT}/verilog_ops/dec_load -Mdir {tmpdir}  -MAKEFLAGS CXX=g++ -CFLAGS -g --assert -CFLAGS " -fPIC" -o {self.sim_path.absolute()} {self.verilog_path.absolute()} {self.object_path.absolute()} {self.cpp_path.absolute()} {HLS_TEST_ROOT}/verilog_ops/float/dpi/float_dpi.cpp {HLS_TEST_ROOT}/src/decoupled/c_sim/hls_stream.cpp {HLS_TEST_ROOT}/src/decoupled/c_sim/hls_decouple.cpp -CFLAGS -DMEMORY_LATENCY={mem_latency}""",
                shell=True, check=True, capture_output=True)
            assert self.sim_path.exists()
        return self.sim_path

    def run_sim(self):
        print(f"running {self.sim_path}")
        assert self.sim_path.exists()
        out = subprocess.run(self.sim_path, capture_output=True, text=True)
        assert out.returncode == 0
        cycles = re.findall("finished - took ([0-9]+) cycles", out.stdout)
        assert len(cycles) == 1
        cycles = int(cycles[0])
        vcd = pathlib.Path(f"V{self.sim_path.stem}.vcd")
        new_vcd = self.sim_path.with_suffix(".hls.vcd")
        assert vcd.exists()
        vcd.replace(new_vcd)
        return cycles

    def upsize_buffers(self, buffer_size: int = 256):
        verilog = self.verilog_path.read_text()
        dot = self.dot_path.read_text()
        buffer_matches = sorted(re.findall(r'((op_HLS_BUF_[^\s]+)\s+(op_HLS_BUF_[^\s]+))\s*\(', verilog))
        for instantiation, module, instance in buffer_matches:
            nrb, dot, verilog = self._resize_buf(instance, buffer_size, dot, verilog)
        load_matches = sorted(re.findall(r'((op_HLS_DEC_LOAD_[^\s]+)\s+(op_HLS_DEC_LOAD_[^\s]+))\s*\(', verilog))
        for instantiation, module, instance in load_matches:
            nrl, dot, verilog = self._resize_load(instance, buffer_size, dot, verilog)
        cpp = self.cpp_path.read_text().replace('".vcd"', '".upsize.vcd"')
        new = self.get_suffix_variant(".upsize")
        new.dot_path.write_text(dot)
        new.verilog_path.write_text(verilog)
        new.cpp_path.write_text(cpp)
        shutil.copy(self.object_path, new.object_path)
        shutil.copy(self.json_path, new.json_path)
        shutil.copy(self.cpp_axi_path, new.cpp_axi_path)
        return new

    def resize_buffers(self):
        vcd = vcdlib.Vcd(self.vcd_path)
        verilog = self.verilog_path.read_text()
        bufs = find_op_names(vcd.net_hier, "op_HLS_BUF")
        buffer_matches = sorted(re.findall(r'((op_HLS_BUF_[^\s]+)\s+(op_HLS_BUF_[^\s]+))\s*\(', verilog))
        assert sorted(bufs) == sorted(instance for instantiation, module, instance in buffer_matches)
        buf_depths = {}
        for buf in bufs:
            node_path = find_name_path(buf, vcd.net_hier)
            # this requires using chisel bufs with a queue that exposes a count
            count_path = node_path + ("queue", "io_count")
            count_vcd = vcd[count_path]
            depth = max((int(val, 2) for ts, val in count_vcd.time_series if ts > vcd.start_time), default=0)
            buf_depths[buf] = depth
            # empty_path = sr_path + (buf, "queue", "empty")
            # empty_vcd = vcd[empty_path]
            # time_steps = 0
            # empty = 0
            # for ts in range(vcd.start_time, vcd.last_change, vcd.time_step*2):
            #     time_steps += 1
            #     if(empty_vcd[ts]):
            #         empty += 1
            # # if depth == 0:
            #     # assert empty == time_steps
            # empty_frac = empty/time_steps
        # pprint.pprint(buf_depths)
        loads = find_op_names(vcd.net_hier, "op_HLS_DEC_LOAD")
        load_matches = sorted(re.findall(r'((op_HLS_DEC_LOAD_[^\s]+)\s+(op_HLS_DEC_LOAD_[^\s]+))\s*\(', verilog))
        assert sorted(loads) == sorted(instance for instantiation, module, instance in load_matches)
        load_depths = {}
        for load in loads:
            node_path = find_name_path(load, vcd.net_hier)
            # this requires using chisel bufs with a queue that exposes a count
            count_path = node_path + ("request_in_flight", "io_count")
            count_vcd = vcd[count_path]
            depth = max((int(val, 2) for ts, val in count_vcd.time_series if ts > vcd.start_time), default=0)
            load_depths[load] = depth
        # pprint.pprint(load_depths)
        dot = self.dot_path.read_text()
        needed_ops = []
        for buf, depth in buf_depths.items():
            rb = RhlsBuf.from_instance(buf)
            if rb.pass_through:
                # we need one more depth somewhere along the path to guarantee good performance
                depth = min(depth + 1, rb.depth)
                # empty or small pass-through buffers are fine
                # todo: can lead to deadlocks for rif loop
                pass
            else:
                # needed to guarantee forward progress
                if depth < 2:
                    depth = 2
                else:
                    depth = min(depth + 1, rb.depth)
            depth = to_pow2(depth)
            nrb, dot, verilog = self._resize_buf(buf, depth, dot, verilog)
            needed_ops.append(nrb.to_chisel())
        for load, depth in load_depths.items():
            depth = to_pow2(depth)
            nrl, dot, verilog = self._resize_load(load, depth, dot, verilog)
            needed_ops.append(nrl.to_chisel())
        # pprint.pprint(collections.Counter(needed_ops))
        # for n in sorted(set(needed_ops)):
        #     print(n)
        cpp = self.cpp_path.read_text()
        if ".upsize" in self.vcd_path.name:
            new = self.get_suffix_variant(".resize")
            cpp = cpp.replace('".upsize.vcd"', '".resize.vcd"')
        else:
            new = self.get_suffix_variant(".downsize")
            cpp = cpp.replace('".vcd"', '".downsize.vcd"')
        new.dot_path.write_text(dot)
        new.verilog_path.write_text(verilog)
        new.cpp_path.write_text(cpp)
        shutil.copy(self.object_path, new.object_path)
        shutil.copy(self.json_path, new.json_path)
        shutil.copy(self.cpp_axi_path, new.cpp_axi_path)
        return new

    def get_suffix_variant(self, suffix):
        extra_suffix = ""
        if self.vcd_path.suffixes[0] != ".hls":
            extra_suffix = self.vcd_path.suffixes[0]
        old_suffix = f"{extra_suffix}.hls"
        return RhlsAccelerator(self.verilog_path.with_name(self.verilog_path.name.replace(old_suffix, f"{suffix}.hls")))

    @staticmethod
    def _resize_buf(buf: str, depth: int, dot: str, verilog: str):
        rb = RhlsBuf.from_instance(buf)
        nrb = dataclasses.replace(rb, depth=depth)
        old_instantiation = rb.to_module() + " " + rb.to_instance() + " "
        new_instantiation = nrb.to_module() + " " + nrb.to_instance() + " "
        assert old_instantiation in verilog
        verilog = verilog.replace(old_instantiation, new_instantiation)
        verilog = verilog.replace(rb.to_instance() + ".", nrb.to_instance() + ".")
        dot = dot.replace(rb.to_instance() + " ", nrb.to_instance() + " ")
        dot = dot.replace(rb.to_instance() + "<", nrb.to_instance() + "<")
        dot = dot.replace(rb.to_instance() + ":", nrb.to_instance() + ":")
        return nrb, dot, verilog

    @staticmethod
    def _resize_load(load: str, depth: int, dot: str, verilog: str):
        rl = RhlsDecLoad.from_instance(load)
        nrl = dataclasses.replace(rl, depth=depth)
        old_instantiation = rl.to_module() + " " + rl.to_instance() + " "
        new_instantiation = nrl.to_module() + " " + nrl.to_instance() + " "
        assert old_instantiation in verilog
        verilog = verilog.replace(old_instantiation, new_instantiation)
        verilog = verilog.replace(rl.to_instance() + ".", nrl.to_instance() + ".")
        dot = dot.replace(rl.to_instance() + " ", nrl.to_instance() + " ")
        dot = dot.replace(rl.to_instance() + "<", nrl.to_instance() + "<")
        dot = dot.replace(rl.to_instance() + ":", nrl.to_instance() + ":")
        return nrl, dot, verilog


build_folder = pathlib.Path("../build")


def resize(verilog: pathlib.Path):
    acc = RhlsAccelerator(verilog)
    if acc.get_suffix_variant(".resize").verilog_path.exists():
        print(f"skipping {acc.sim_path}")
        return
    print("starting", verilog)
    up_acc = acc.upsize_buffers(128)
    up_acc.build_sim()
    up_cycles = up_acc.run_sim()
    print(verilog, up_cycles, "cycles")
    # assert up_cycles <= cycles
    re_acc = up_acc.resize_buffers()
    up_acc.remove()


def downsize(verilog: pathlib.Path):
    acc = RhlsAccelerator(verilog)
    if acc.get_suffix_variant(".downsize").verilog_path.exists():
        print(f"skipping {acc.sim_path}")
        return
    print("starting", verilog)
    acc.build_sim()
    cycles = acc.run_sim()
    print(verilog, cycles, "cycles")
    re_acc = acc.resize_buffers()

    # re_acc.build_sim()
    # re_cycles = re_acc.run_sim()
    # print(re_acc.verilog_path, re_cycles, "cycles")


def main():
    verilogs = sorted(build_folder.glob("decoupled/*.hls.v"))
    targets = []
    for verilog in verilogs:
        if verilog.suffixes[0] != ".hls":
            continue
        # acc = RhlsAccelerator(verilog)
        # if acc.get_suffix_variant(".downsize").verilog_path.exists():
        #     print(f"skipping {acc.sim_path}")
        #     continue
        targets.append(verilog)

    pool = multiprocessing.Pool(8)
    pool.map(downsize, targets, chunksize=1)
    # pool.map(resize, targets, chunksize=1)
    pool.close()


if __name__ == '__main__':
    main()
