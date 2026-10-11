"""Check full-control RSID launch on a complete C64, without a Python IRQ wrapper."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('compiler', type=Path)
    parser.add_argument('vice', type=Path)
    parser.add_argument('sid', type=Path)
    parser.add_argument('--song', type=int, default=0)
    parser.add_argument('--stop-address', type=lambda x: int(x, 16), required=True)
    parser.add_argument('--bytes', action='append', default=[], metavar='ADDRESS=HEX')
    parser.add_argument('--copy-check', action='store_true', help='compare full load image before init executes')
    parser.add_argument('--port', type=lambda x:int(x,16), help='expected CPU port value, not RAM beneath the 6510 ports')
    parser.add_argument('--video-standard', choices=['pal','ntsc'], default='pal')
    parser.add_argument('--audio-output', type=Path)
    args = parser.parse_args()
    (ROOT/'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'build') as tmp:
        p = Path(tmp).resolve()
        prg, mapping, ram, log = [p/name for name in ('test.PRG','test.map','ram.bin','monitor.log')]
        subprocess.run([str(args.compiler.resolve()),'sid',str(args.sid.resolve()),'--song',str(args.song),
                        '-o',str(prg),'--map',str(mapping)],check=True)
        fields = {parts[0]: parts[1:] for line in mapping.read_text().splitlines() if (parts:=line.split())}
        commands = p/'commands.mon'
        commands.write_text('\n'.join([f'logname "{log.as_posix()}"','log on',
                                       f'load "{prg.as_posix()}" 0',
                                       f'break exec {args.stop_address:04x}','g 080d','r','io d400',
                                       'bank ram',f'bsave "{ram.as_posix()}" 0 0000 ffff','quit','']),encoding='ascii')
        sound = ['+sound','-warp']
        if args.audio_output:
            args.audio_output = args.audio_output.resolve()
            args.audio_output.parent.mkdir(parents=True,exist_ok=True)
            sound = ['-sound','-sounddev','wav','-soundarg',str(args.audio_output),'-soundoutput','1']
        result = subprocess.run([str(args.vice.resolve()),'-default',*sound,'-'+args.video_standard,'-logfile',str(p/'vice.log'),
                                 '-initbreak','ready','-moncommands',str(commands),'-limitcycles','16000000'],
                                capture_output=True,timeout=40)
        if not ram.exists():
            diagnostic=(result.stdout+result.stderr).decode(errors='replace')
            for f in (p/'vice.log',log):
                if f.exists(): diagnostic+='\n'+f.read_text(errors='replace')
            raise RuntimeError(f'VICE did not reach native stop address (exit {result.returncode}):\n'+diagnostic[-4000:])
        memory = ram.read_bytes()
        assert len(memory)==65536
        if args.port is not None:
            registers=re.findall(r'\.;[0-9a-f]{4}\s+([^\r\n]+)',log.read_text(errors='replace'),re.I)
            assert registers and int(registers[-1].split()[5],16)==args.port, 'CPU port differs: '+str(registers)
        for check in args.bytes:
            address, wanted = check.split('=',1)
            start, expected = int(address,16),bytes.fromhex(wanted)
            assert memory[start:start+len(expected)]==expected, f'RAM {address}: wanted {wanted}, got {memory[start:start+len(expected)].hex(" ")}'
        if args.copy_check:
            data=args.sid.read_bytes()
            load=int(fields['rsid_payload'][0][1:],16)
            payload=data[126:]
            assert memory[load:load+len(payload)]==payload, 'RSID image differs after RAM/ROM/I/O transfer'
        if args.audio_output:
            with wave.open(str(args.audio_output),'rb') as audio:
                pcm=audio.readframes(audio.getnframes())
                assert audio.getsampwidth()==2 and len(pcm)>4096 and any(pcm), 'SID audio is empty/silent'
        print('VICE standalone RSID passed:',args.sid.name,args.video_standard,'native stop',hex(args.stop_address))


if __name__=='__main__':
    main()
