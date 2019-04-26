import sys
import time
import subprocess
import signal
import logging
from threading import Thread




logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)-15s [%(levelname)s] %(message)s',
)
logger = logging.getLogger(__name__)


class ProcessHasNotBeenCreated(Exception):
    pass

class ProcessHasNotBeenStartedSuccessfully(Exception):
    pass


def process_is_running(process):
    """ 
    Returns:
        A tuple of (result, returncode) where 
        - is_running is equal to True if the process is running and False if
        the process has terminated,
        - returncode is None if the process is running and the actual value 
        of returncode if the process has terminated.
    """
    is_running = True
    returncode = process.poll()
    if returncode is not None:
        is_running = False
    return (is_running, returncode)



def create_process(name, args):
    """ 
    name: name of the application being started
    args: process args

    Raises:
        ProcessHasNotBeenCreated
        ProcessHasNotBeenStarted
    """
    cf = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == 'win32' else None

    try:
        logger.debug('Starting process: {}'.format(name))
        if sys.platform == 'win32':
            process = subprocess.Popen(
                args, 
                stdin =subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=False,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
                bufsize=1
            )
        else:
            process = subprocess.Popen(
                args, 
                stdin =subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=False,
                bufsize=1
            )
    except OSError as e:
        raise ProcessHasNotBeenCreated('{}. Error: {}'.format(name, e))

    # Check that the process has started successfully and has not terminated
    # because of an error
    time.sleep(1)
    logger.debug('Checking that the process has started successfully: {}'.format(name))
    is_running, returncode = process_is_running(process)
    if not is_running:
        raise ProcessHasNotBeenStartedSuccessfully(
            "{}, returncode {}, stderr: {}".format(name, returncode, process.stderr.readlines())
        )

    logger.debug('Started successfully')
    return process




def cleanup_process(name, process):
    """ 
    Clean up actions for the process. 

    Raises:
        ProcessHasNotBeenKilled
    """

    # FIXME: Signals may not work on Windows properly. Might be useful
    # https://stefan.sofa-rockers.org/2013/08/15/handling-sub-process-hierarchies-python-linux-os-x/
    #process.stdout.close()
    #process.stdin.close()
    logger.debug('Terminating the process: {}'.format(name))
    logger.debug('OS: {}'.format(sys.platform))

    sig = signal.CTRL_C_EVENT if sys.platform == 'win32' else signal.SIGINT
    #if sys.platform == 'win32':
    #    if sig in [signal.SIGINT, signal.CTRL_C_EVENT]:
    #        sig = signal.CTRL_C_EVENT
    #    elif sig in [signal.SIGBREAK, signal.CTRL_BREAK_EVENT]:
    #        sig = signal.CTRL_BREAK_EVENT
    #    else:
    #        sig = signal.SIGTERM

    process.send_signal(sig)
    for i in range(3):
        time.sleep(1)
        is_running, returncode = process_is_running(process)
        if not is_running: 
            logger.debug('Terminated')
            return

    # TODO: (For future) Experiment with this more. If stransmit will not 
    # stop after several terminations, there is a problem, and kill() will
    # hide this problem in this case.
    
    # TODO: (!) There is a problem with tsp, it's actually not killed
    # however process_is_running(process) becomes False
    is_running, _ = process_is_running(process)
    if is_running:
        logger.debug('Killing the process: {}'.format(name))
        process.kill()
        time.sleep(1)
    is_running, _ = process_is_running(process)
    if is_running:
        raise ProcessHasNotBeenKilled('{}, id: {}'.format(name, process.pid))
    logger.debug('Killed')



def main():
    logger.info("Starting srt-live-transmit localhost autotest")
    #logger.debug(f'Parsing config file {config}')
    #config_path = pathlib.Path(config)
    #config_data = load_config_data(config_path)
    #args = ["srt-live-transmit.exe", "file://con", "srt://:4200"]
    args = ["./srt-live-transmit", "file://con", "srt://:4200"]
    snd_srt_process = create_process('srt-live-transmit (SND)', args)

    args = ["./srt-live-transmit", "srt://127.0.0.1:4200", "file://con"]
    rcv_srt_process = create_process('srt-live-transmit (RCV)', args)

    time.sleep(3)

    index = 0
    maxLoops = 1500

    # Byte objects in Python
    # https://www.devdungeon.com/content/working-binary-data-python
    buffer = bytearray([(1 + i % 255) for i in range(0, 1315)]) + bytearray([0])

    logger.debug("RCV: {}".format(rcv_srt_process.stderr.readline()))
    logger.debug("SND: {}".format(snd_srt_process.stderr.readline()))

    # The following is not working. Somehow the messages do not appear on stderr.
    # srt-live-transmit prints 'SRT source connected' and '"SRT target connected"'
    # to stderr, but the following reading functions from the pipes never return.
    # Added flushing to srt-live-transmit, but does not help.
    # Therefore have to comment this out until solved.
    #logger.debug("RCV: {}".format(rcv_srt_process.stderr.readline()))
    #logger.debug("SND: {}".format(snd_srt_process.stderr.readline()))

    is_valid = True

    def background_stuff(is_valid):
        target_values = buffer.copy()
        for i in range(0, maxLoops):
            target_values[0] = 1 + i % 255
            data = rcv_srt_process.stdout.read(1316)
            is_valid = target_values == data
            message = "Packet {}  size {} ".format(i, len(data))
            if is_valid:
                logger.debug(message + "valid")
            else:
                logger.error(message + "invalid")
                logger.info("RECEIVED: {}".format(data))
                logger.info("EXPECTED: {}".format(target_values))
                return

    t = Thread(target=background_stuff, args=[is_valid])
    t.start()

    while index < maxLoops and is_valid:
        time.sleep(0.01)
        # Send data to the subprocess
        logger.debug('Sending bytestring {}'.format(index))
        buffer[0] = 1 + index % 255
        snd_srt_process.stdin.write(buffer)
        snd_srt_process.stdin.flush()
        index += 1

    t.join()

    logger.info("file://con srt-live-transmit -> srt -> srt-live-transmit -> file://con: validation {}".format("succeeded" if is_valid else "failed"))

    cleanup_process('srt-live-transmit (SND)', snd_srt_process)
    cleanup_process('srt-live-transmit (RCV)', rcv_srt_process)

    sys.exit(0) if is_valid else sys.exit(1)


if __name__ == '__main__':
    main()




