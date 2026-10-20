import concurrent.futures
import contextlib
import multiprocessing
import os
import queue
import shutil
import sys
import tempfile
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'cook'))
sys.path.insert(0, str(HERE))
import cook
import tools.install_backend as install_backend
from tools.install_backend import *

class _InstallModule(sys.modules[__name__].__class__):
    def __setattr__(self, name, value):
        super().__setattr__(name, value)
        backend = self.__dict__.get('install_backend')
        if backend is not None and hasattr(backend, name):
            setattr(backend, name, value)

sys.modules[__name__].__class__ = _InstallModule


def self_test():
    """Exercise imports and frozen multiprocessing without a headset or game."""
    import tkinter
    tkinter.Tcl()  # Verify bundled Tcl without opening a window/display.
    with tempfile.TemporaryDirectory() as scratch:
        src = Path(scratch) / 'input.txt'
        dst = Path(scratch) / 'output.txt'
        src.write_text('GalaxyQuest installer worker test', encoding='utf-8')
        with concurrent.futures.ProcessPoolExecutor(max_workers=2,
                mp_context=multiprocessing.get_context('spawn')) as pool:
            stats, notes, error = pool.submit(cook.process, (str(src), str(dst), 'copy')).result(timeout=60)
        assert error is None and stats['copied'] == 1 and dst.read_bytes() == src.read_bytes()
    if getattr(sys, 'frozen', False):
        assert find_apk().is_file()
        assert Path(find_adb()).is_file()
        assert '--gameonly' in dolphin_command(find_dolphin_tool(), 'extract', '--help')


class Installer:
    def __init__(self, root):
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
        self.tk, self.ttk = tk, ttk
        self.filedialog, self.messagebox = filedialog, messagebox
        self.root, self.events = root, queue.Queue()
        self.busy = False
        root.title('GalaxyQuest installer')
        root.geometry('760x560')
        root.minsize(650, 510)
        root.protocol('WM_DELETE_WINDOW', self.close)
        panel = ttk.Frame(root, padding=24)
        panel.pack(fill='both', expand=True)
        panel.columnconfigure(0, weight=1)
        panel.rowconfigure(12, weight=1)
        ttk.Label(panel, text='Install GalaxyQuest', font=('', 22, 'bold')).grid(row=0, column=0, sticky='w')
        ttk.Label(panel, text='Choose your game copy, connect your headset, then click Install.',
                  wraplength=670).grid(row=1, column=0, columnspan=2, sticky='w', pady=(6, 16))
        ttk.Label(panel, text='Your Super Mario Galaxy copy (ISO, RVZ or WBFS)').grid(row=2, column=0, sticky='w')
        self.folder = tk.StringVar()
        self.folder_entry = ttk.Entry(panel, textvariable=self.folder)
        self.folder_entry.grid(row=3, column=0, sticky='ew', pady=(4, 12))
        self.browse = ttk.Button(panel, text='Browse...', command=self.pick_game)
        self.browse.grid(row=3, column=1, padx=(8, 0), pady=(4, 12))
        self.movies = tk.BooleanVar(value=True)
        self.movies_check = ttk.Checkbutton(panel, text='Include opening and ending movies (recommended)', variable=self.movies)
        self.folder_button = ttk.Button(panel, text='Use an already extracted folder...', command=self.pick_folder)
        self.folder_button.grid(row=4, column=0, columnspan=2, sticky='w', pady=(0, 12))
        self.movies_check.grid(row=5, column=0, columnspan=2, sticky='w')
        ttk.Label(panel, text='Headset (developer mode and USB debugging must be enabled)').grid(row=6, column=0, sticky='w', pady=(16, 4))
        self.device = tk.StringVar()
        self.devices = ttk.Combobox(panel, textvariable=self.device, state='readonly')
        self.devices.grid(row=7, column=0, sticky='ew')
        self.refresh_button = ttk.Button(panel, text='Refresh', command=self.refresh)
        self.refresh_button.grid(row=7, column=1, padx=(8, 0))
        ttk.Button(panel, text='Developer mode help', command=lambda: webbrowser.open(DEVELOPER_HELP)).grid(row=8, column=0, sticky='w', pady=(4, 12))
        self.install_button = ttk.Button(panel, text='Install', command=self.start)
        self.install_button.grid(row=9, column=0, sticky='w', pady=(0, 8))
        self.status = tk.StringVar(value='Ready. Your game will be converted and copied to the headset.')
        ttk.Label(panel, textvariable=self.status, wraplength=670).grid(row=10, column=0, columnspan=2, sticky='w')
        self.bar = ttk.Progressbar(panel, maximum=100)
        self.bar.grid(row=11, column=0, columnspan=2, sticky='ew', pady=(8, 10))
        self.log = tk.Text(panel, height=6, state='disabled', wrap='word')
        self.log.grid(row=12, column=0, columnspan=2, sticky='nsew')
        self.root.after(100, self.drain)
        self.root.after(200, self.refresh)

    def pick_game(self):
        path = self.filedialog.askopenfilename(title='Choose your Super Mario Galaxy copy',
            filetypes=[('Game disc images', '*.iso *.rvz *.wbfs *.gcz *.wia *.ciso'), ('All files', '*')])
        if path:
            self.folder.set(path)

    def pick_folder(self):
        path = self.filedialog.askdirectory(title='Choose the extracted game folder', mustexist=True)
        if path:
            self.folder.set(path)

    def controls(self, enabled):
        state = 'normal' if enabled else 'disabled'
        for widget in (self.folder_entry, self.browse, self.movies_check, self.refresh_button,
                       self.folder_button, self.install_button):
            widget.configure(state=state)
        self.devices.configure(state='readonly' if enabled else 'disabled')

    def refresh(self):
        self.refresh_button.configure(state='disabled')

        def detect():
            try:
                devices = list_devices(find_adb())
                self.events.put(('devices', devices))
            except Exception as error:
                self.events.put(('detect-error', str(error)))
        threading.Thread(target=detect, daemon=True).start()

    def start(self):
        if not self.folder.get().strip():
            self.messagebox.showinfo('Choose your game', 'Click Browse and select your game copy.')
            return
        args = (self.folder.get(), self.device.get(), self.movies.get())
        self.busy = True
        self.controls(False)
        self.bar.configure(mode='determinate', value=0)
        self.log.configure(state='normal')
        self.log.delete('1.0', 'end')
        self.log.configure(state='disabled')
        threading.Thread(target=self.worker, args=args, daemon=True).start()

    def worker(self, folder, selected, movies):
        log_path = None
        try:
            # Logs survive cleanup and can be attached to bug reports.
            logs = cache_dir() / 'logs'
            logs.mkdir(parents=True, exist_ok=True)
            stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
            log_path = logs / ('install-%s.log' % stamp)
            with log_path.open('w', encoding='utf-8', buffering=1) as stream:
                with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
                    print('GalaxyQuest installer\nPython: %s\nGame: %s\nMovies: %s' %
                          (sys.version, folder, movies), flush=True)
                    try:
                        install_game(folder, selected, movies,
                                     lambda text, percent: self.events.put(('progress', text, percent)))
                    except BaseException:
                        traceback.print_exc()
                        raise
            self.events.put(('success', str(log_path)))
        except BaseException as error:
            detail = str(error)
            if isinstance(error, subprocess.CalledProcessError):
                detail = error.output or detail
            self.events.put(('error', detail, str(log_path) if log_path else 'Could not create a log.'))

    def drain(self):
        try:
            while True:
                event = self.events.get_nowait()
                kind = event[0]
                if kind == 'progress':
                    self.status.set(event[1])
                    self.bar.stop()
                    self.bar.configure(mode='determinate', value=event[2])
                    if event[1].startswith(('Extracting', 'Preparing', 'Copying', 'Finishing', 'Installing')):
                        self.bar.configure(mode='indeterminate')
                        self.bar.start(20)
                elif kind == 'devices':
                    if self.busy:
                        continue
                    ready = [serial for serial, state, _ in event[1] if state == 'device']
                    self.devices.configure(values=ready)
                    if self.device.get() not in ready:
                        self.device.set(ready[0] if len(ready) == 1 else '')
                    try:
                        choose_device(event[1], self.device.get())
                        self.status.set('Headset connected. Choose your game copy and click Install.')
                    except InstallError as error:
                        self.status.set(str(error))
                    if not self.busy:
                        self.refresh_button.configure(state='normal')
                elif kind == 'detect-error':
                    if self.busy:
                        continue
                    self.status.set(event[1])
                    if not self.busy:
                        self.refresh_button.configure(state='normal')
                elif kind in ('success', 'error'):
                    self.busy = False
                    self.controls(True)
                    self.bar.stop()
                    self.bar.configure(mode='determinate', value=100 if kind == 'success' else 0)
                    log_path = event[1] if kind == 'success' else event[2]
                    self.log.configure(state='normal')
                    if Path(log_path).is_file():
                        self.log.insert('end', Path(log_path).read_text(encoding='utf-8')[-16000:])
                    self.log.insert('end', '\nLog: ' + log_path)
                    self.log.see('end')
                    self.log.configure(state='disabled')
                    if kind == 'error':
                        self.status.set('Installation stopped. Fix the issue below and click Install to retry.')
                        self.messagebox.showerror('Installation stopped', event[1] + '\n\nLog: ' + log_path)
                    else:
                        self.messagebox.showinfo('GalaxyQuest installed',
                            'Open Library > Unknown Sources > GalaxyQuest on your headset.\n\nLog: ' + log_path)
        except queue.Empty:
            pass
        self.root.after(100, self.drain)

    def close(self):
        if self.busy:
            self.messagebox.showinfo('Installation running', 'Please wait for installation to finish before closing this window.')
            return
        self.root.destroy()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--self-test', action='store_true', help='test the packaged converter and worker processes')
    ap.add_argument('--self-test-report', type=Path, help='write the self-test result to a file (for windowed builds)')
    ap.add_argument('--convert-only', nargs=2, metavar=('GAME', 'OUTPUT'), help='extract and convert without connecting a headset')
    ap.add_argument('--without-movies', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        try:
            self_test()
            report, result = 'PASS: converter imports, Tcl, worker processes and bundled assets\n', 0
        except BaseException:
            report, result = traceback.format_exc(), 1
        if args.self_test_report:
            args.self_test_report.write_text(report, encoding='utf-8')
        elif sys.stdout is not None:
            print(report)
        return result
    if args.convert_only:
        # No console in Windows builds: write conversion details beside output.
        output = Path(args.convert_only[1]).resolve()
        if output.exists():
            ap.error('--convert-only requires a new output folder so existing files are preserved')
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.with_name(output.name + '-conversion.log').open('w', encoding='utf-8', buffering=1) as log:
            with contextlib.redirect_stdout(log), contextlib.redirect_stderr(log):
                source, is_image = game_copy(args.convert_only[0])
                with temporary_workspace(source, not args.without_movies) as scratch:
                    if is_image:
                        source = extract_disc(source, scratch / 'extracted')
                    return cook.convert(str(source), str(output),
                                        jobs=min(os.cpu_count() or 1, 8), with_movies=not args.without_movies)
    import tkinter as tk
    root = tk.Tk()
    Installer(root)
    root.mainloop()
    return 0


if __name__ == '__main__':
    sys.exit(main())
