/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1(the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis,WITHOUT WARRANTY OF ANY KIND,either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * Alternatively,the contents of this file may be used under the terms
 * of the GNU General Public License(the "GPL"),in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License,indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above,a recipient may use your
 * version of this file under either the License or the GPL.
 *
 *   Vlad Seryakov vlad@cryatalballinc.com
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <netinet/in.h>

#include <asm/ioctls.h>
#include <sys/socket.h>
#include <termios.h>
#include <linux/cdk.h>
#include <linux/cdrom.h>
#include <linux/fd.h>
#include <linux/lp.h>
#include <linux/sockios.h>
#include <linux/soundcard.h>
#include <linux/videodev.h>
#include <linux/vt.h>

#include <linux/videodev.h>
#include <png.h>

#ifndef LOG_AUTHPRIV
# define LOG_AUTHPRIV LOG_AUTH
#endif
#include "tcl.h"

static int NsSysInterpInit(Tcl_Interp * interp, void *context);

#ifdef TCL_STANDALONE

int Tclsys_Init(Tcl_Interp * interp)
{
    char *argv0;
    static int initialized = 0;

    if (initialized) {
        return 0;
    }
    initialized = 1;
    NsSysInterpInit(interp, 0);
    return 0;
}

#else

#include "ns.h"

NS_EXPORT int Ns_ModuleVersion = 1;

NS_EXPORT int Ns_ModuleInit(char *hServer, char *hModule)
{
    Ns_TclRegisterTrace(hServer, NsSysInterpInit, 0, NS_TCL_TRACE_CREATE);
    return NS_OK;
}

#endif

typedef struct {
    Tcl_Interp *interp;
    unsigned char *buf;
    char *file;
    char *device;
    int fd;
    int norm;
    int width;
    int height;
    int depth;
    int input;
    int mmap;
    int mbufsize;
    int framesize;
    int maxinputs;
    unsigned int brightness;
    unsigned int contrast;
} V4L;

static volatile char v4l_lock_flag = 0;

static struct syslog_data {
    int opened;
    int facility;
    int options;
    char ident[32];
} syslog_data;

/*
 * sys_ioctl
 *
 * Manipulate the underlying device parameters of special files.
 *
 * Usage:
 *   sys_ioctl dev req ?arg?
 *
 *  dev      - open device channel
 *  req      - request
 *
 * Returns:
 *    None
*/
static int SysIoctl(ClientData data, Tcl_Interp * interp, int objc, Tcl_Obj * CONST objv[])
{
    static struct {
        unsigned int req;
        char *label;
    } ioctlMap[] = {
        {  FIOSETOWN, "FIOSETOWN"},        // const int *
        {  SIOCSPGRP, "SIOCSPGRP"},        // const int *
        {  FIOGETOWN, "FIOGETOWN"},        // int *
        {  SIOCGPGRP, "SIOCGPGRP"},        // int *
        {  SIOCATMARK, "SIOCATMARK"},      // int *
        {  SIOCGSTAMP, "SIOCGSTAMP"},      // timeval *
        {  TCGETS, "TCGETS"},      // struct termios *
        {  TCSETS, "TCSETS"},      // const struct termios *
        {  TCSETSW, "TCSETSW"},    // const struct termios *
        {  TCSETSF, "TCSETSF"},    // const struct termios *
        {  TCGETA, "TCGETA"},      // struct termio *
        {  TCSETA, "TCSETA"},      // const struct termio *
        {  TCSETAW, "TCSETAW"},    // const struct termio *
        {  TCSETAF, "TCSETAF"},    // const struct termio *
        {  TCSBRK, "TCSBRK"},      // int
        {  TCXONC, "TCXONC"},      // int
        {  TCFLSH, "TCFLSH"},      // int
        {  TIOCEXCL, "TIOCEXCL"},  // void
        {  TIOCNXCL, "TIOCNXCL"},  // void
        {  TIOCSCTTY, "TIOCSCTTY"},        // int
        {  TIOCGPGRP, "TIOCGPGRP"},        // pid_t *
        {  TIOCSPGRP, "TIOCSPGRP"},        // const pid_t *
        {  TIOCOUTQ, "TIOCOUTQ"},  // int *
        {  TIOCSTI, "TIOCSTI"},    // const char *
        {  TIOCGWINSZ, "TIOCGWINSZ"},      // struct winsize *
        {  TIOCSWINSZ, "TIOCSWINSZ"},      // const struct winsize *
        {  TIOCMGET, "TIOCMGET"},  // int *
        {  TIOCMBIS, "TIOCMBIS"},  // const int *
        {  TIOCMBIC, "TIOCMBIC"},  // const int *
        {  TIOCMSET, "TIOCMSET"},  // const int *
        {  TIOCGSOFTCAR, "TIOCGSOFTCAR"},  // int *
        {  TIOCSSOFTCAR, "TIOCSSOFTCAR"},  // const int *
        {  FIONREAD, "FIONREAD"},  // int *
        {  TIOCINQ, "TIOCINQ"},    // int *
        {  TIOCLINUX, "TIOCLINUX"},        // const char * // MORE
        {  TIOCCONS, "TIOCCONS"},  // void
        {  TIOCGSERIAL, "TIOCGSERIAL"},    // struct serial_struct *
        {  TIOCSSERIAL, "TIOCSSERIAL"},    // const struct serial_struct *
        {  TIOCPKT, "TIOCPKT"},    // const int *
        {  FIONBIO, "FIONBIO"},    // const int *
        {  TIOCNOTTY, "TIOCNOTTY"},        // void
        {  TIOCSETD, "TIOCSETD"},  // const int *
        {  TIOCGETD, "TIOCGETD"},  // int *
        {  TCSBRKP, "TCSBRKP"},    // int
        {  FIONCLEX, "FIONCLEX"},  // void
        {  FIOCLEX, "FIOCLEX"},    // void
        {  FIOASYNC, "FIOASYNC"},  // const int *
        {  TIOCSERCONFIG, "TIOCSERCONFIG"},        // void
        {  TIOCSERGWILD, "TIOCSERGWILD"},  // int *
        {  TIOCSERSWILD, "TIOCSERSWILD"},  // const int *
        {  TIOCGLCKTRMIOS, "TIOCGLCKTRMIOS"},      // struct termios *
        {  TIOCSLCKTRMIOS, "TIOCSLCKTRMIOS"},      // const struct termios *
        {  TIOCSERGSTRUCT, "TIOCSERGSTRUCT"},      // struct async_struct *
        {  TIOCSERGETLSR, "TIOCSERGETLSR"},        // int *
        {  TIOCSERGETMULTI, "TIOCSERGETMULTI"},    // struct serial_multiport_struct *
        {  TIOCSERSETMULTI, "TIOCSERSETMULTI"},    // const struct serial_multiport_struct *
        {  STL_BINTR, "STL_BINTR"},        // void
        {  STL_BSTART, "STL_BSTART"},      // void
        {  STL_BSTOP, "STL_BSTOP"},        // void
        {  STL_BRESET, "STL_BRESET"},      // void
        {  CDROMPAUSE, "CDROMPAUSE"},      // void
        {  CDROMRESUME, "CDROMRESUME"},    // void
        {  CDROMPLAYMSF, "CDROMPLAYMSF"},  // const struct cdrom_msf *
        {  CDROMPLAYTRKIND, "CDROMPLAYTRKIND"},    // const struct cdrom_ti *
        {  CDROMREADTOCHDR, "CDROMREADTOCHDR"},    // struct cdrom_tochdr *
        {  CDROMREADTOCENTRY, "CDROMREADTOCENTRY"},        // struct cdrom_tocentry *
        {  CDROMSTOP, "CDROMSTOP"},        // void
        {  CDROMSTART, "CDROMSTART"},      // void
        {  CDROMEJECT, "CDROMEJECT"},      // void
        {  CDROMVOLCTRL, "CDROMVOLCTRL"},  // const struct cdrom_volctrl *
        {  CDROMSUBCHNL, "CDROMSUBCHNL"},  // struct cdrom_subchnl *
        {  CDROMREADMODE2, "CDROMREADMODE2"},      // const struct cdrom_msf * //
        {  CDROMREADMODE1, "CDROMREADMODE1"},      // const struct cdrom_msf * //
        {  CDROMREADAUDIO, "CDROMREADAUDIO"},      // const struct cdrom_read_audio * //
        {  CDROMEJECT_SW, "CDROMEJECT_SW"},        // int
        {  CDROMMULTISESSION, "CDROMMULTISESSION"},        // struct cdrom_multisession *
        {  CDROM_GET_UPC, "CDROM_GET_UPC"},        // struct { char [8]; }
        {  CDROMRESET, "CDROMRESET"},      // void
        {  CDROMVOLREAD, "CDROMVOLREAD"},  // struct cdrom_volctrl *
        {  CDROMREADRAW, "CDROMREADRAW"},  // const struct cdrom_msf * //
        {  CDROMREADCOOKED, "CDROMREADCOOKED"},    // const struct cdrom_msf * //
        {  CDROMSEEK, "CDROMSEEK"},        // const struct cdrom_msf *
        {  FDCLRPRM, "FDCLRPRM"},  // void
        {  FDSETPRM, "FDSETPRM"},  // const struct floppy_struct *
        {  FDDEFPRM, "FDDEFPRM"},  // const struct floppy_struct *
        {  FDGETPRM, "FDGETPRM"},  // struct floppy_struct *
        {  FDMSGON, "FDMSGON"},    // void
        {  FDMSGOFF, "FDMSGOFF"},  // void
        {  FDFMTBEG, "FDFMTBEG"},  // void
        {  FDFMTTRK, "FDFMTTRK"},  // const struct format_descr *
        {  FDFMTEND, "FDFMTEND"},  // void
        {  FDSETEMSGTRESH, "FDSETEMSGTRESH"},      // int
        {  FDFLUSH, "FDFLUSH"},    // void
        {  FDSETMAXERRS, "FDSETMAXERRS"},  // const struct floppy_max_errors *
        {  FDGETMAXERRS, "FDGETMAXERRS"},  // struct floppy_max_errors *
        {  FDGETDRVTYP, "FDGETDRVTYP"},    // struct { char [16]; }
        {  FDSETDRVPRM, "FDSETDRVPRM"},    // const struct floppy_drive_params *
        {  FDGETDRVPRM, "FDGETDRVPRM"},    // struct floppy_drive_params *
        {  FDGETDRVSTAT, "FDGETDRVSTAT"},  // struct floppy_drive_struct *
        {  FDPOLLDRVSTAT, "FDPOLLDRVSTAT"},        // struct floppy_drive_struct *
        {  FDRESET, "FDRESET"},    // int
        {  FDGETFDCSTAT, "FDGETFDCSTAT"},  // struct floppy_fdc_state *
        {  FDWERRORCLR, "FDWERRORCLR"},    // void
        {  FDWERRORGET, "FDWERRORGET"},    // struct floppy_write_errors *
        {  FDRAWCMD, "FDRAWCMD"},  // struct floppy_raw_cmd * // MORE
        {  FDTWADDLE, "FDTWADDLE"},        // void
        {  LPCHAR, "LPCHAR"},      // int
        {  LPTIME, "LPTIME"},      // int
        {  LPABORT, "LPABORT"},    // int
        {  LPSETIRQ, "LPSETIRQ"},  // int
        {  LPGETIRQ, "LPGETIRQ"},  // int *
        {  LPWAIT, "LPWAIT"},      // int
        {  LPCAREFUL, "LPCAREFUL"},        // int
        {  LPABORTOPEN, "LPABORTOPEN"},    // int
        {  LPGETSTATUS, "LPGETSTATUS"},    // int *
        {  LPRESET, "LPRESET"},    // void
        {  CDROMAUDIOBUFSIZ, "CDROMAUDIOBUFSIZ"},  // int
        {  SIOCADDRT, "SIOCADDRT"},        // const struct rtentry * //
        {  SIOCDELRT, "SIOCDELRT"},        // const struct rtentry * //
        {  SIOCGIFNAME, "SIOCGIFNAME"},    // char []
        {  SIOCSIFLINK, "SIOCSIFLINK"},    // void
        {  SIOCGIFCONF, "SIOCGIFCONF"},    // struct ifconf * // MORE
        {  SIOCGIFFLAGS, "SIOCGIFFLAGS"},  // struct ifreq *
        {  SIOCSIFFLAGS, "SIOCSIFFLAGS"},  // const struct ifreq *
        {  SIOCGIFADDR, "SIOCGIFADDR"},    // struct ifreq *
        {  SIOCSIFADDR, "SIOCSIFADDR"},    // const struct ifreq *
        {  SIOCGIFDSTADDR, "SIOCGIFDSTADDR"},      // struct ifreq *
        {  SIOCSIFDSTADDR, "SIOCSIFDSTADDR"},      // const struct ifreq *
        {  SIOCGIFBRDADDR, "SIOCGIFBRDADDR"},      // struct ifreq *
        {  SIOCSIFBRDADDR, "SIOCSIFBRDADDR"},      // const struct ifreq *
        {  SIOCGIFNETMASK, "SIOCGIFNETMASK"},      // struct ifreq *
        {  SIOCSIFNETMASK, "SIOCSIFNETMASK"},      // const struct ifreq *
        {  SIOCGIFMETRIC, "SIOCGIFMETRIC"},        // struct ifreq *
        {  SIOCSIFMETRIC, "SIOCSIFMETRIC"},        // const struct ifreq *
        {  SIOCGIFMEM, "SIOCGIFMEM"},      // struct ifreq *
        {  SIOCSIFMEM, "SIOCSIFMEM"},      // const struct ifreq *
        {  SIOCGIFMTU, "SIOCGIFMTU"},      // struct ifreq *
        {  SIOCSIFMTU, "SIOCSIFMTU"},      // const struct ifreq *
        {  SIOCSIFHWADDR, "SIOCSIFHWADDR"},        // const struct ifreq *
        {  SIOCGIFENCAP, "SIOCGIFENCAP"},  // int *
        {  SIOCSIFENCAP, "SIOCSIFENCAP"},  // const int *
        {  SIOCGIFHWADDR, "SIOCGIFHWADDR"},        // struct ifreq *
        {  SIOCGIFSLAVE, "SIOCGIFSLAVE"},  // void
        {  SIOCSIFSLAVE, "SIOCSIFSLAVE"},  // void
        {  SIOCADDMULTI, "SIOCADDMULTI"},  // const struct ifreq *
        {  SIOCDELMULTI, "SIOCDELMULTI"},  // const struct ifreq *
        {  SIOCDARP, "SIOCDARP"},  // const struct arpreq *
        {  SIOCGARP, "SIOCGARP"},  // struct arpreq *
        {  SIOCSARP, "SIOCSARP"},  // const struct arpreq *
        {  SIOCDRARP, "SIOCDRARP"},        // const struct arpreq *
        {  SIOCGRARP, "SIOCGRARP"},        // struct arpreq *
        {  SIOCSRARP, "SIOCSRARP"},        // const struct arpreq *
        {  SIOCGIFMAP, "SIOCGIFMAP"},      // struct ifreq *
        {  SIOCSIFMAP, "SIOCSIFMAP"},      // const struct ifreq *
        {  SNDCTL_SEQ_RESET, "SNDCTL_SEQ_RESET"},  // void
        {  SNDCTL_SEQ_SYNC, "SNDCTL_SEQ_SYNC"},    // void
        {  SNDCTL_SYNTH_INFO, "SNDCTL_SYNTH_INFO"},        // struct synth_info *
        {  SNDCTL_SEQ_CTRLRATE, "SNDCTL_SEQ_CTRLRATE"},    // int *
        {  SNDCTL_SEQ_GETOUTCOUNT, "SNDCTL_SEQ_GETOUTCOUNT"},      // int *
        {  SNDCTL_SEQ_GETINCOUNT, "SNDCTL_SEQ_GETINCOUNT"},        // int *
        {  SNDCTL_SEQ_PERCMODE, "SNDCTL_SEQ_PERCMODE"},    // void
        {  SNDCTL_FM_LOAD_INSTR, "SNDCTL_FM_LOAD_INSTR"},  // const struct sbi_instrument *
        {  SNDCTL_SEQ_TESTMIDI, "SNDCTL_SEQ_TESTMIDI"},    // const int *
        {  SNDCTL_SEQ_RESETSAMPLES, "SNDCTL_SEQ_RESETSAMPLES"},    // const int *
        {  SNDCTL_SEQ_NRSYNTHS, "SNDCTL_SEQ_NRSYNTHS"},    // int *
        {  SNDCTL_SEQ_NRMIDIS, "SNDCTL_SEQ_NRMIDIS"},      // int *
        {  SNDCTL_MIDI_INFO, "SNDCTL_MIDI_INFO"},  // struct midi_info *
        {  SNDCTL_SEQ_THRESHOLD, "SNDCTL_SEQ_THRESHOLD"},  // const int *
        {  SNDCTL_SYNTH_MEMAVL, "SNDCTL_SYNTH_MEMAVL"},    // int *
        {  SNDCTL_FM_4OP_ENABLE, "SNDCTL_FM_4OP_ENABLE"},  // const int *
        {  SNDCTL_SEQ_PANIC, "SNDCTL_SEQ_PANIC"},  // void
        {  SNDCTL_SEQ_OUTOFBAND, "SNDCTL_SEQ_OUTOFBAND"},  // const struct seq_event_rec *
        {  SNDCTL_TMR_TIMEBASE, "SNDCTL_TMR_TIMEBASE"},    // int *
        {  SNDCTL_TMR_START, "SNDCTL_TMR_START"},  // void
        {  SNDCTL_TMR_STOP, "SNDCTL_TMR_STOP"},    // void
        {  SNDCTL_TMR_CONTINUE, "SNDCTL_TMR_CONTINUE"},    // void
        {  SNDCTL_TMR_TEMPO, "SNDCTL_TMR_TEMPO"},  // int *
        {  SNDCTL_TMR_SOURCE, "SNDCTL_TMR_SOURCE"},        // int *
        {  SNDCTL_TMR_METRONOME, "SNDCTL_TMR_METRONOME"},  // const int *
        {  SNDCTL_TMR_SELECT, "SNDCTL_TMR_SELECT"},        // int *
        {  SNDCTL_MIDI_PRETIME, "SNDCTL_MIDI_PRETIME"},    // int *
        {  SNDCTL_MIDI_MPUMODE, "SNDCTL_MIDI_MPUMODE"},    // const int *
        {  SNDCTL_MIDI_MPUCMD, "SNDCTL_MIDI_MPUCMD"},      // struct mpu_command_rec *
        {  SNDCTL_DSP_RESET, "SNDCTL_DSP_RESET"},  // void
        {  SNDCTL_DSP_SYNC, "SNDCTL_DSP_SYNC"},    // void
        {  SNDCTL_DSP_SPEED, "SNDCTL_DSP_SPEED"},  // int *
        {  SNDCTL_DSP_STEREO, "SNDCTL_DSP_STEREO"},        // int *
        {  SNDCTL_DSP_GETBLKSIZE, "SNDCTL_DSP_GETBLKSIZE"},        // int *
        {  SOUND_PCM_WRITE_CHANNELS, "SOUND_PCM_WRITE_CHANNELS"},  // int *
        {  SOUND_PCM_WRITE_FILTER, "SOUND_PCM_WRITE_FILTER"},      // int *
        {  SNDCTL_DSP_POST, "SNDCTL_DSP_POST"},    // void
        {  SNDCTL_DSP_SUBDIVIDE, "SNDCTL_DSP_SUBDIVIDE"},  // int *
        {  SNDCTL_DSP_SETFRAGMENT, "SNDCTL_DSP_SETFRAGMENT"},      // int *
        {  SNDCTL_DSP_GETFMTS, "SNDCTL_DSP_GETFMTS"},      // int *
        {  SNDCTL_DSP_SETFMT, "SNDCTL_DSP_SETFMT"},        // int *
        {  SNDCTL_DSP_GETOSPACE, "SNDCTL_DSP_GETOSPACE"},  // struct audio_buf_info *
        {  SNDCTL_DSP_GETISPACE, "SNDCTL_DSP_GETISPACE"},  // struct audio_buf_info *
        {  SNDCTL_DSP_NONBLOCK, "SNDCTL_DSP_NONBLOCK"},    // void
        {  SOUND_PCM_READ_RATE, "SOUND_PCM_READ_RATE"},    // int *
        {  SOUND_PCM_READ_CHANNELS, "SOUND_PCM_READ_CHANNELS"},    // int *
        {  SOUND_PCM_READ_BITS, "SOUND_PCM_READ_BITS"},    // int *
        {  SOUND_PCM_READ_FILTER, "SOUND_PCM_READ_FILTER"},        // int *
        {  SNDCTL_COPR_RESET, "SNDCTL_COPR_RESET"},        // void
        {  SNDCTL_COPR_LOAD, "SNDCTL_COPR_LOAD"},  // const struct copr_buffer *
        {  SNDCTL_COPR_RDATA, "SNDCTL_COPR_RDATA"},        // struct copr_debug_buf *
        {  SNDCTL_COPR_RCODE, "SNDCTL_COPR_RCODE"},        // struct copr_debug_buf *
        {  SNDCTL_COPR_WDATA, "SNDCTL_COPR_WDATA"},        // const struct copr_debug_buf *
        {  SNDCTL_COPR_WCODE, "SNDCTL_COPR_WCODE"},        // const struct copr_debug_buf *
        {  SNDCTL_COPR_RUN, "SNDCTL_COPR_RUN"},    // struct copr_debug_buf *
        {  SNDCTL_COPR_HALT, "SNDCTL_COPR_HALT"},  // struct copr_debug_buf *
        {  SNDCTL_COPR_SENDMSG, "SNDCTL_COPR_SENDMSG"},    // const struct copr_msg *
        {  SNDCTL_COPR_RCVMSG, "SNDCTL_COPR_RCVMSG"},      // struct copr_msg *
        {  SOUND_MIXER_READ_VOLUME, "SOUND_MIXER_READ_VOLUME"},    // int *
        {  SOUND_MIXER_READ_BASS, "SOUND_MIXER_READ_BASS"},        // int *
        {  SOUND_MIXER_READ_TREBLE, "SOUND_MIXER_READ_TREBLE"},    // int *
        {  SOUND_MIXER_READ_SYNTH, "SOUND_MIXER_READ_SYNTH"},      // int *
        {  SOUND_MIXER_READ_PCM, "SOUND_MIXER_READ_PCM"},  // int *
        {  SOUND_MIXER_READ_SPEAKER, "SOUND_MIXER_READ_SPEAKER"},  // int *
        {  SOUND_MIXER_READ_LINE, "SOUND_MIXER_READ_LINE"},        // int *
        {  SOUND_MIXER_READ_MIC, "SOUND_MIXER_READ_MIC"},  // int *
        {  SOUND_MIXER_READ_CD, "SOUND_MIXER_READ_CD"},    // int *
        {  SOUND_MIXER_READ_IMIX, "SOUND_MIXER_READ_IMIX"},        // int *
        {  SOUND_MIXER_READ_ALTPCM, "SOUND_MIXER_READ_ALTPCM"},    // int *
        {  SOUND_MIXER_READ_RECLEV, "SOUND_MIXER_READ_RECLEV"},    // int *
        {  SOUND_MIXER_READ_IGAIN, "SOUND_MIXER_READ_IGAIN"},      // int *
        {  SOUND_MIXER_READ_OGAIN, "SOUND_MIXER_READ_OGAIN"},      // int *
        {  SOUND_MIXER_READ_LINE1, "SOUND_MIXER_READ_LINE1"},      // int *
        {  SOUND_MIXER_READ_LINE2, "SOUND_MIXER_READ_LINE2"},      // int *
        {  SOUND_MIXER_READ_LINE3, "SOUND_MIXER_READ_LINE3"},      // int *
        {  SOUND_MIXER_READ_MUTE, "SOUND_MIXER_READ_MUTE"},        // int *
        {  SOUND_MIXER_READ_ENHANCE, "SOUND_MIXER_READ_ENHANCE"},  // int *
        {  SOUND_MIXER_READ_LOUD, "SOUND_MIXER_READ_LOUD"},        // int *
        {  SOUND_MIXER_READ_RECSRC, "SOUND_MIXER_READ_RECSRC"},    // int *
        {  SOUND_MIXER_READ_DEVMASK, "SOUND_MIXER_READ_DEVMASK"},  // int *
        {  SOUND_MIXER_READ_RECMASK, "SOUND_MIXER_READ_RECMASK"},  // int *
        {  SOUND_MIXER_READ_STEREODEVS, "SOUND_MIXER_READ_STEREODEVS"},    // int *
        {  SOUND_MIXER_READ_CAPS, "SOUND_MIXER_READ_CAPS"},        // int *
        {  SOUND_MIXER_WRITE_VOLUME, "SOUND_MIXER_WRITE_VOLUME"},  // int *
        {  SOUND_MIXER_WRITE_BASS, "SOUND_MIXER_WRITE_BASS"},      // int *
        {  SOUND_MIXER_WRITE_TREBLE, "SOUND_MIXER_WRITE_TREBLE"},  // int *
        {  SOUND_MIXER_WRITE_SYNTH, "SOUND_MIXER_WRITE_SYNTH"},    // int *
        {  SOUND_MIXER_WRITE_PCM, "SOUND_MIXER_WRITE_PCM"},        // int *
        {  SOUND_MIXER_WRITE_SPEAKER, "SOUND_MIXER_WRITE_SPEAKER"},        // int *
        {  SOUND_MIXER_WRITE_LINE, "SOUND_MIXER_WRITE_LINE"},      // int *
        {  SOUND_MIXER_WRITE_MIC, "SOUND_MIXER_WRITE_MIC"},        // int *
        {  SOUND_MIXER_WRITE_CD, "SOUND_MIXER_WRITE_CD"},  // int *
        {  SOUND_MIXER_WRITE_IMIX, "SOUND_MIXER_WRITE_IMIX"},      // int *
        {  SOUND_MIXER_WRITE_ALTPCM, "SOUND_MIXER_WRITE_ALTPCM"},  // int *
        {  SOUND_MIXER_WRITE_RECLEV, "SOUND_MIXER_WRITE_RECLEV"},  // int *
        {  SOUND_MIXER_WRITE_IGAIN, "SOUND_MIXER_WRITE_IGAIN"},    // int *
        {  SOUND_MIXER_WRITE_OGAIN, "SOUND_MIXER_WRITE_OGAIN"},    // int *
        {  SOUND_MIXER_WRITE_LINE1, "SOUND_MIXER_WRITE_LINE1"},    // int *
        {  SOUND_MIXER_WRITE_LINE2, "SOUND_MIXER_WRITE_LINE2"},    // int *
        {  SOUND_MIXER_WRITE_LINE3, "SOUND_MIXER_WRITE_LINE3"},    // int *
        {  SOUND_MIXER_WRITE_MUTE, "SOUND_MIXER_WRITE_MUTE"},      // int *
        {  SOUND_MIXER_WRITE_ENHANCE, "SOUND_MIXER_WRITE_ENHANCE"},        // int *
        {  SOUND_MIXER_WRITE_LOUD, "SOUND_MIXER_WRITE_LOUD"},      // int *
        {  SOUND_MIXER_WRITE_RECSRC, "SOUND_MIXER_WRITE_RECSRC"},  // int *
        {  VT_OPENQRY, "VT_OPENQRY"},      // int *
        {  VT_GETMODE, "VT_GETMODE"},      // struct vt_mode *
        {  VT_SETMODE, "VT_SETMODE"},      // const struct vt_mode *
        {  VT_GETSTATE, "VT_GETSTATE"},    // struct vt_stat *
        {  VT_SENDSIG, "VT_SENDSIG"},      // void
        {  VT_RELDISP, "VT_RELDISP"},      // int
        {  VT_ACTIVATE, "VT_ACTIVATE"},    // int
        {  VT_WAITACTIVE, "VT_WAITACTIVE"},        // int
        {  VT_DISALLOCATE, "VT_DISALLOCATE"},      // int
        {  VT_RESIZE, "VT_RESIZE"},        // const struct vt_sizes *
        {  VT_RESIZEX, "VT_RESIZEX"},      // const struct vt_consize *
        {  VIDIOCGCAP, "VIDIOCGCAP"},      // struct video_capability *
        {  VIDIOCGCHAN, "VIDIOCGCHAN"},    // struct video_channel *
        {  VIDIOCSCHAN, "VIDIOCSCHAN"},    // struct video_channel *
        {  VIDIOCGTUNER, "VIDIOCGTUNER"},  // struct video_tuner *
        {  VIDIOCSTUNER, "VIDIOCSTUNER"},  // struct video_tuner *
        {  VIDIOCGPICT, "VIDIOCGPICT"},    // struct video_picture *
        {  VIDIOCSPICT, "VIDIOCSPICT"},    // struct video_picture *
        {  VIDIOCCAPTURE, "VIDIOCCAPTURE"},        // int *
        {  VIDIOCGWIN, "VIDIOCGWIN"},      // struct video_window *
        {  VIDIOCSWIN, "VIDIOCSWIN"},      // struct video_window *
        {  VIDIOCGFBUF, "VIDIOCGFBUF"},    // struct video_buffer *
        {  VIDIOCSFBUF, "VIDIOCSFBUF"},    // struct video_buffer *
        {  VIDIOCKEY, "VIDIOCKEY"},        // struct video_key *
        {  VIDIOCGFREQ, "VIDIOCGFREQ"},    // unsigned long *
        {  VIDIOCSFREQ, "VIDIOCSFREQ"},    // unsigned long *
        {  VIDIOCGAUDIO, "VIDIOCGAUDIO"},  // struct video_audio *
        {  VIDIOCSAUDIO, "VIDIOCSAUDIO"},  // struct video_audio *
        {  VIDIOCSYNC, "VIDIOCSYNC"},      // int *
        {  VIDIOCMCAPTURE, "VIDIOCMCAPTURE"},      // struct video_mmap *
        {  VIDIOCGMBUF, "VIDIOCGMBUF"},    // struct video_mbuf *
        {  VIDIOCGUNIT, "VIDIOCGUNIT"},    // struct video_unit *
        {  VIDIOCGCAPTURE, "VIDIOCGCAPTURE"},      // struct video_capture *
        {  VIDIOCSCAPTURE, "VIDIOCSCAPTURE"},      // struct video_capture *
        {  VIDIOCSPLAYMODE, "VIDIOCSPLAYMODE"},    // struct video_play_mode *
        {  VIDIOCSWRITEMODE, "VIDIOCSWRITEMODE"},  // int *
        {  VIDIOCGPLAYINFO, "VIDIOCGPLAYINFO"},    // struct video_info *
        {  VIDIOCSMICROCODE, "VIDIOCSMICROCODE"},  // struct video_code *
        {  VIDIOCGVBIFMT, "VIDIOCGVBIFMT"},        // struct vbi_format *
        {  VIDIOCSVBIFMT, "VIDIOCSVBIFMT"},        // struct vbi_format *
        {  0, 0}
    };
    Tcl_Channel chan;
    int i, fd, mode, len, req = 0;
    char *sreq = 0;
    unsigned char *arg = 0;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "dev req ?arg?");
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, Tcl_GetString(objv[1]), &mode);
    if (!chan || !(mode & TCL_READABLE) || !(mode & TCL_WRITABLE)) {
        Tcl_AppendResult(interp, "file must be readable and writable", 0);
        return TCL_ERROR;
    }
    /* Retrieve fd */
    Tcl_GetChannelHandle(chan, TCL_WRITABLE, (ClientData) & fd);
    /* Retrieve request */
    sreq = Tcl_GetString(objv[2]);
    for (i = 0; ioctlMap[i].label; i++) {
        if (!strcmp(sreq, ioctlMap[i].label)) {
            req = ioctlMap[i].req;
            break;
        }
    }
    if (req == 0 && Tcl_GetInt(interp, sreq, &req) != TCL_OK)
        return TCL_ERROR;
    if (objc > 3)
        arg = Tcl_GetByteArrayFromObj(objv[3], &len);
    if (ioctl(fd, req, arg) == -1) {
        Tcl_AppendResult(interp, "sys_ioctl: ", sreq, ": ", strerror(errno), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * sys_statfs
 *
 * Returns information about a mounted file system.
 *
 * Usage:
 *   sys_statfs path
 *
 *  path is the path name of any file within the mounted  filesystem.
 *
 * Returns:
 *  list with file system info as name value pairs
 *      f_bsize    file system block size
 *      f_frsize   fragment size
 *      f_blocks   size of fs in f_frsize units
 *      f_bfree    # free blocks
 *      f_bavail   # free blocks for non-root
 *      f_files    # inodes
 *      f_ffree    # free inodes
 *      f_favail   # free inodes for non-root
 *      f_fsid     file system id
 *      f_flag     mount flags
 *      f_namemax  maximum filename length
*/
static int SysStatFs(ClientData data, Tcl_Interp * interp, int argc, const char **argv)
{
    char str[128];
    struct statvfs vfs;

    if (argc != 2) {
        Tcl_AppendResult(interp, argv[0], " path", NULL);
        return TCL_ERROR;
    }
    if (statvfs(argv[1], &vfs)) {
        Tcl_AppendResult(interp, "sys_statfs: ", strerror(errno), NULL);
        return TCL_ERROR;
    }
    Tcl_AppendResult(interp, "path ", argv[1], " ", 0);
    sprintf(str, "%lu", vfs.f_bsize);
    Tcl_AppendResult(interp, "f_bsize ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_frsize);
    Tcl_AppendResult(interp, "f_frsize ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_blocks);
    Tcl_AppendResult(interp, "f_blocks ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_bfree);
    Tcl_AppendResult(interp, "f_bfree ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_bavail);
    Tcl_AppendResult(interp, "f_bavail ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_files);
    Tcl_AppendResult(interp, "f_files ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_ffree);
    Tcl_AppendResult(interp, "f_ffree ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_favail);
    Tcl_AppendResult(interp, "f_favail ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_fsid);
    Tcl_AppendResult(interp, "f_fsid ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_flag);
    Tcl_AppendResult(interp, "f_flag ", str, " ", 0);
    sprintf(str, "%lu", vfs.f_namemax);
    Tcl_AppendResult(interp, "f_namemax ", str, " ", 0);
    return TCL_OK;
}

/*
 * sys_log
 *
 * Usage:
 *   sys_log -facility f -options o -ident i priority message
 *
 *  facility    - kernel, cron, authpriv, mail, local0, local1, daemon, local2,
 *                news, local3, local4, local5, local6, syslog, local7, auth, uucp, lpr, user
 *  options     - list with any of { CONS NDELAY PERROR PID ODELAY NOWAIT }
 *  ident       - ident is prepended to every message, and is typically the program name
 *  priority    - info, alert, emerg, err, notice, warning, error, crit, debug
 *
 * Returns:
 *   nothing
 */
static int SysLog(ClientData data, Tcl_Interp * interp, int argc, const char **argv)
{
    int i, j, priority = 0;
    static struct {
        char *label;
        unsigned int facility;
    } facilityMap[] = {
        { "auth", LOG_AUTH },
        { "authpriv", LOG_AUTHPRIV },
        { "cron", LOG_CRON },
        { "daemon", LOG_DAEMON },
        { "kernel", LOG_KERN },
        { "lpr", LOG_LPR },
        { "mail", LOG_MAIL },
        { "news", LOG_NEWS },
        { "syslog", LOG_SYSLOG },
        { "user", LOG_USER },
        { "uucp", LOG_UUCP },
        { "local0", LOG_LOCAL0 },
        { "local1", LOG_LOCAL1 },
        { "local2", LOG_LOCAL2 },
        { "local3", LOG_LOCAL3 },
        { "local4", LOG_LOCAL4 },
        { "local5", LOG_LOCAL5 },
        { "local6", LOG_LOCAL6 },
        { "local7", LOG_LOCAL7 },
        { NULL, 0 }
    };
    static struct {
        char *label;
        unsigned int priority;
    } priorityMap[] = {
        { "emerg", LOG_EMERG },
        { "alert", LOG_ALERT },
        { "crit", LOG_CRIT },
        { "err", LOG_ERR },
        { "error", LOG_ERR },
        { "warning", LOG_WARNING },
        { "notice", LOG_NOTICE },
        { "info", LOG_INFO },
        { "debug", LOG_DEBUG },
        { NULL, 0 }
    };
    static struct {
        char *label;
        unsigned int option;
    } optionMap[] = {
        { "CONS",  LOG_CONS },
        { "NDELAY", LOG_NDELAY },
        { "PERROR", LOG_PERROR },
        { "PID", LOG_PID },
        { "ODELAY", LOG_ODELAY },
        { "NOWAIT", LOG_NOWAIT },
        { NULL, 0 }
    };
    if (argc < 2) {
        Tcl_AppendResult(interp, argv[0], " ?-facility f? ?-options o? ?-ident i? priority message", 0);
        return TCL_ERROR;
    }
    for (i = 1; i < argc - 1;) {
        if (!strcmp(argv[i], "-facility")) {
            for (j = 0; facilityMap[j].label; j++) {
                if (!strcasecmp(facilityMap[j].label, argv[i + 1])) {
                    break;
                }
            }
            if (!facilityMap[j].label) {
                Tcl_AppendResult(interp, "wrong facilities: ", argv[i + 1], ": should be one of: ", NULL);
                for (j = 0; facilityMap[j].label; j++) {
                    Tcl_AppendResult(interp, facilityMap[j].label, " ", NULL);
                }
                return TCL_ERROR;
            }
            syslog_data.facility = (int) facilityMap[j].facility;
            closelog();
            syslog_data.opened = 0;
            i += 2;
            continue;
        }
        if (!strcmp(argv[i], "-options")) {
            syslog_data.options = 0;
            for (j = 0; optionMap[j].label; j++) {
                if (!strstr(argv[i + 1], optionMap[j].label)) {
                    syslog_data.options |= optionMap[j].option;
                }
            }
            closelog();
            syslog_data.opened = 0;
            i += 2;
            continue;
        }
        if (!strcmp(argv[i], "-ident")) {
            memset(syslog_data.ident, 0, sizeof(syslog_data.ident));
            strncpy(syslog_data.ident, argv[i + 1], sizeof(syslog_data.ident) - 1);
            closelog();
            syslog_data.opened = 0;
            i += 2;
            continue;
        }
        break;
    }
    if (i < argc) {
        for (j = 0; priorityMap[j].label; j++) {
            if (!strcasecmp(priorityMap[j].label, argv[i])) {
                break;
            }
        }
        if (!priorityMap[j].label) {
            Tcl_AppendResult(interp, "wrong priority ", argv[i], ": should be one of: ", NULL);
            for (j = 0; priorityMap[j].label; j++) {
                Tcl_AppendResult(interp, priorityMap[j].label, " ", NULL);
            }
            return TCL_ERROR;
        }
        priority = (int) priorityMap[j].priority;
        i++;
    }
    if (i < argc) {
        if (!syslog_data.opened) {
            openlog(syslog_data.ident, syslog_data.options, syslog_data.facility);
            syslog_data.opened = 1;
        }
        syslog(priority, argv[i]);
        return TCL_OK;
    }
    return TCL_OK;
}

static int v4l_running()
{
    return v4l_lock_flag > 0;
}

static int v4l_save(V4L * v)
{
    int i;
    FILE *fd;
    png_infop ip;
    png_structp pp;
    unsigned char *p = v->buf;

    if ((fd = fopen(v->file, "w+")) == NULL) {
        Tcl_AppendResult(v->interp, "fopen: ", v->file, ": ", strerror(errno), 0);
        return -2;
    }
    if (!(pp = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0))) {
        Tcl_AppendResult(v->interp, "png write create:", strerror(errno), 0);
        fclose(fd);
        return -1;
    }
    if (!(ip = png_create_info_struct(pp))) {
        Tcl_AppendResult(v->interp, "png info create:", strerror(errno), 0);
        png_destroy_write_struct(&pp, 0);
        fclose(fd);
        return -1;
    }
    png_init_io(pp, fd);
    png_set_IHDR(pp, ip, v->width, v->height, 8, v->depth == 1 ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_bgr(pp);
    png_write_info(pp, ip);
    for (i = 0; i < v->height; i++) {
        png_write_row(pp, p);
        p += v->width * v->depth;
    }
    png_write_end(pp, ip);
    png_destroy_write_struct(&pp, &ip);
    fclose(fd);
    return 0;
}

static int v4l_init(V4L * v)
{
    int tries = 5;
    struct video_capability vid_caps;
    struct video_channel vid_chnl;
    struct video_picture vid_pict;
    struct video_window vid_win;
    struct video_mbuf vid_buf;

    v4l_lock_flag = 1;
    while ((v->fd = open(v->device, O_RDWR)) == -1 && --tries)
        sleep(1);
    if (v->fd == -1) {
        Tcl_AppendResult(v->interp, "open: ", v->device, ": ", strerror(errno), 0);
        return -1;
    }
    if (ioctl(v->fd, VIDIOCGCAP, &vid_caps) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCGCAP: ", strerror(errno), 0);
        return -2;
    }
    v->maxinputs = vid_caps.channels;
    if (ioctl(v->fd, VIDIOCGPICT, &vid_pict) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCGPICT: ", strerror(errno), 0);
        return -3;
    }
    vid_pict.depth = v->depth * 8;
    vid_pict.brightness = v->brightness;
    vid_pict.contrast = v->contrast;
    vid_pict.palette = (v->depth == 1) ? VIDEO_PALETTE_GREY : VIDEO_PALETTE_RGB24;
    if (ioctl(v->fd, VIDIOCSPICT, &vid_pict) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCSPICT: ", strerror(errno), 0);
        return -4;
    }
    vid_chnl.channel = v->input;
    if (ioctl(v->fd, VIDIOCGCHAN, &vid_chnl) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCGCHAN: ", strerror(errno), 0);
    }
    vid_chnl.channel = v->input;
    vid_chnl.norm = v->norm;
    if (ioctl(v->fd, VIDIOCSCHAN, &vid_chnl) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCSCHAN: ", strerror(errno), 0);
        return -5;
    }
    if (!v->mmap || ioctl(v->fd, VIDIOCGMBUF, &vid_buf) == -1) {
        v->mmap = 0;
        if (ioctl(v->fd, VIDIOCGWIN, &vid_win) != -1) {
            vid_win.width = v->width;
            vid_win.height = v->height;
            if (ioctl(v->fd, VIDIOCSWIN, &vid_win) == -1) {
                Tcl_AppendResult(v->interp, "VIDIOCSWIN: ", strerror(errno), 0);
                return -6;
            }
        }
    } else {
        v->mmap = 1;
        v->mbufsize = vid_buf.size;
    }
    v->framesize = v->width * v->height * v->depth;
    v->buf = malloc(v->framesize);
    return 0;
}

static void v4l_free(V4L * v)
{
    if (v->buf)
        free(v->buf);
    if (v->fd > 0)
        close(v->fd);
    v4l_lock_flag = 0;
}

static int v4l_grab(V4L * v)
{
    char *map;
    struct video_mmap vid_mmap;

    if (!v->mmap) {
        if (read(v->fd, v->buf, v->framesize) <= 0) {
            Tcl_AppendResult(v->interp, "read: ", strerror(errno), 0);
            return -1;
        }
        return 0;
    }
    map = mmap(0, v->mbufsize, PROT_READ | PROT_WRITE, MAP_SHARED, v->fd, 0);
    if ((unsigned char *) -1 == (unsigned char *) map) {
        Tcl_AppendResult(v->interp, "mmap: ", strerror(errno), 0);
        return -1;
    }
    vid_mmap.format = (v->depth == 1) ? VIDEO_PALETTE_GREY : VIDEO_PALETTE_RGB24;
    vid_mmap.frame = 0;
    vid_mmap.width = v->width;
    vid_mmap.height = v->height;
    if (ioctl(v->fd, VIDIOCMCAPTURE, &vid_mmap) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCMCAPTURE: ", strerror(errno), 0);
        munmap(map, v->mbufsize);
        return -7;
    }
    if (ioctl(v->fd, VIDIOCSYNC, &vid_mmap) == -1) {
        Tcl_AppendResult(v->interp, "VIDIOCSYNC: ", strerror(errno), 0);
        munmap(map, v->mbufsize);
        return -8;
    }
    memcpy(v->buf, map, v->framesize);
    munmap(map, v->mbufsize);
    return 0;
}

/*
 * sys_v4lgrab
 *
 * Grab a image from Video4Linux device.
 *
 * Usage:
 *   sys_v4lgrab grab ?-device d -file f -width w -height h -norm n -input i -mmap 0|1 -brightness b -contrast c -depth d?
 *
 *  device     - default /dev/video0
 *  mmap       - default 0, if 1 use mmap, otherwise use read
 *  norm       - default 1, 0 = PAL; 1 = NTSC; 2 = SECAM; 3 = PAL-Nc; 4 = PAL-M; 5 = PAL-N; 6 = NTSC-JP; 7 = PAL-60;
 *  input      - defaut 1, 0 = Television; 1 = Composite1; 2 = S-Video; 3 = Composite3;
 *  width      - default 320
 *  height     - default 200
 *  brightness - default 32768, max 65536
 *  contrast   - default 32768, max 65536
 *  depth      - default 3, can be 1, 2 or 3
 *  file       - default v4lgrab.png
 *
 *  sys_v4l lock
 *
 *  sys_v4l unlock
 *
 *  sys_v4l running
 *
 * Returns:
 *    None
*/
static int SysV4L(ClientData data, Tcl_Interp * interp, int objc, Tcl_Obj * CONST objv[])
{
    int i;
    V4L v;
    char *arg, *val;

    arg = Tcl_GetString(objv[1]);

    if (!strcmp(arg, "grab")) {
        memset(&v, 0, sizeof(v));
        v.interp = interp;
        v.file = "v4lgrab.png";
        v.device = "/dev/video0";
        v.width = 320;
        v.height = 200;
        v.depth = 3;
        v.input = 1;
        v.norm = VIDEO_MODE_NTSC;
        v.brightness = 32768;
        v.contrast = 32768;
        v.mmap = 0;

        for (i = 2; i < objc - 1; i++) {
            arg = Tcl_GetString(objv[i]);
            val = Tcl_GetString(objv[i + 1]);
            if (!strcmp(arg, "-device"))
                v.device = val;
            else if (!strcmp(arg, "-file"))
                v.file = val;
            else if (!strcmp(arg, "-width"))
                v.width = atoi(val);
            else if (!strcmp(arg, "-height"))
                v.height = atoi(val);
            else if (!strcmp(arg, "-norm"))
                v.norm = atoi(val);
            else if (!strcmp(arg, "-input"))
                v.input = atoi(val);
            else if (!strcmp(arg, "-brightness"))
                v.brightness = atoi(val);
            else if (!strcmp(arg, "-contrast"))
                v.contrast = atoi(val);
            else if (!strcmp(arg, "-depth"))
                v.depth = atoi(val);
            else if (!strcmp(arg, "-mmap"))
                v.mmap = atoi(val);
        }
        if (v4l_init(&v) || v4l_grab(&v) || v4l_save(&v)) {
            v4l_free(&v);
            return TCL_ERROR;
        }
        v4l_free(&v);
        return TCL_OK;
    }

    if (!strcmp(arg, "running")) {
        Tcl_AppendResult(interp, v4l_running()? "1" : "0", 0);
        return TCL_OK;
    }

    return TCL_ERROR;
}

/*
 * sys_xevent (idea is taken from Android Extensions to Tk)
 *
 *  Transmits events to the X server, which will translate them and send them on to other
 *  applications as if the user had triggered with the keyboard or mouse.
 *
 * Usage:
 *   sys_xevent ?wait ms? ?mouse x,y? ?key keysym? ?keydn keysym? ?keyup keysym? ?btndn btn? ?btnup btn? ?click btn? ?display dsp? ?type {string}?
 *
 *  ms     - miliseconds for wait
 *  x,y    - x and y coordinates for mouse
 *  keysym - X11 key symbol from /usr/X11R6/lib/X11/XKeysymDB
 *  btn    - mouse button number
 *  dsp    - X11 display name
 *
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

static char *latin_xlt[] = {
    "space", "exclam", "quotedbl", "numbersign", "dollar", "percent", "ampersand",
    "apostrophe", "parenleft", "parenright", "asterisk", "plus",
    "comma", "minus", "period", "slash", "0", "1", "2", "3", "4", "5", "6", "7",
    "8", "9", "colon", "semicolon", "less", "equal", "greater", "question", "at",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "bracketleft",
    "backslash", "bracketright", "asciicircum", "underscore", "grave",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "braceleft", "bar",
    "braceright", "asciitilde"
};

static char *shifted = "~!@#$%^&*()_+|}{\":?><ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static int SysXevent(ClientData data, Tcl_Interp * interp, int objc, Tcl_Obj * CONST objv[])
{
    char *op, *arg;
    int i, value, shift, x, y;
    Display *display = XOpenDisplay(":0.0");

    for (i = 1; i < objc; i += 2) {
        op = Tcl_GetString(objv[i]);
        arg = Tcl_GetString(objv[i + 1]);

        if (!strncmp("key", op, 3)) {   /* keyup, keydn, or key */
            if (!(value = atoi(arg))) {
                if ((value = XStringToKeysym(arg)) != NoSymbol)
                    value = XKeysymToKeycode(display, value);
            }
            if (!value) {
                Tcl_AppendResult(interp, "unknown key ", arg, 0);
                XCloseDisplay(display);
                return TCL_ERROR;
            }
            switch (op[3]) {
            case 'd':
                XTestFakeKeyEvent(display, value, True, 0);
                break;
            case 'u':
                XTestFakeKeyEvent(display, value, False, 0);
                break;
            default:
                XTestFakeKeyEvent(display, value, True, 0);
                XTestFakeKeyEvent(display, value, False, 0);
                break;
            }
        } else
         if (!strcmp("mouse", op)) {    /* move mouse */
            x = atoi(arg);
            if ((arg = strchr(arg, ',')))
                y = 0;
            else
                y = atoi(++arg);
            XTestFakeMotionEvent(display, 0, x, y, 0);
        } else
         if (!strcmp(op, "btndn")) {    /* mouse down */
            x = atoi(arg);
            XTestFakeButtonEvent(display, x, True, 0);
        } else
         if (!strcmp(op, "btnup")) {    /* mouse up */
            x = atoi(arg);
            XTestFakeButtonEvent(display, x, False, 0);
        } else
         if (!strcmp(op, "click")) {    /* mouse click */
            x = atoi(arg);
            XTestFakeButtonEvent(display, x, True, 0);
            XTestFakeButtonEvent(display, x, False, 0);
        } else
         if (!strcmp(op, "display")) {  /* use display */
            XCloseDisplay(display);
            display = XOpenDisplay(arg);
            if (!display) {
                Tcl_AppendResult(interp, "unable to open display ", arg);
                XCloseDisplay(display);
                return TCL_ERROR;
            }
        } else
         if (!strcmp(op, "wait")) {     /* wait x milliseconds */
            usleep(atoi(arg) * 1000);
        } else
         if (!strcmp(op, "type")) {     /* type a string */
            for (i = 0; arg[i]; i++) {
                shift = 0;
                if (strchr(shifted, arg[i])) {
                    shift = XStringToKeysym("Shift_L");
                    shift = XKeysymToKeycode(display, shift);
                    XTestFakeKeyEvent(display, shift, True, 0);
                }
                value = XStringToKeysym(latin_xlt[(int) arg[i] - (int) ' ']);
                value = XKeysymToKeycode(display, value);
                XTestFakeKeyEvent(display, value, True, 0);
                XTestFakeKeyEvent(display, value, False, 0);
                if (shift)
                    XTestFakeKeyEvent(display, shift, False, 0);
            }
        }
        XTestFlush(display);
        XFlush(display);
        Tcl_GlobalEval(interp, "update");
    }
    XCloseDisplay(display);
    return TCL_OK;
}

/*
 * sys_udp
 *
 *  Sends UDP packet
 *
 * Usage:
 *   sys_udp ipaddr port data ?-timeout N? ?-retries N? ?-noreply 1|0?
 *
 *  ipaddr   - host ipaddr
 *  port     - UDP port
 *  data     - data to send
 *  -timeout - how long to wait reply in seconds
 *  -retries - how many times to retry on timeout
 *  -noreply - if set do not wait for reply
 *
 */

static int SysUdp(ClientData arg, Tcl_Interp * interp, int objc, Tcl_Obj * CONST objv[])
{
    fd_set fds;
    char buf[16384];
    struct timeval tv;
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    char *address = 0, *port = 0, *data = 0;
    int i, sock, len, timeout = 5, retries = 1, noreply = 0;

    if (objc < 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "ipaddr port data ?-timeout N? ?-retries N? ?-noreply 1|0?");
        return TCL_ERROR;
    }
    address = Tcl_GetString(objv[1]);
    port = Tcl_GetString(objv[2]);
    data = Tcl_GetStringFromObj(objv[3], &len);
    for (i = 4; i < objc - 1; i += 2) {
        if (!strcmp(Tcl_GetString(objv[i]), "-timeout")) {
            timeout = atoi(Tcl_GetString(objv[i + 1]));
            continue;
        }
        if (!strcmp(Tcl_GetString(objv[i]), "-retries")) {
            retries = atoi(Tcl_GetString(objv[i + 1]));
            continue;
        }
        if (!strcmp(Tcl_GetString(objv[i]), "-noreply")) {
            noreply = atoi(Tcl_GetString(objv[i + 1]));
            continue;
        }
        break;
    }
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(port));
    sa.sin_addr.s_addr = inet_addr(address);
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        Tcl_AppendResult(interp, "socket error: ", address, ":", port, " ", strerror(errno), 0);
        return TCL_ERROR;
    }
    // To support brodcasting
    i = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &i, sizeof(int));
  resend:
    if (sendto(sock, data, len, 0, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
        Tcl_AppendResult(interp, "sendto error: ", address, ":", port, " ", strerror(errno), 0);
        close(sock);
        return TCL_ERROR;
    }
    if (noreply) {
        close(sock);
        return TCL_OK;
    }
    memset(buf, 0, sizeof(buf));
  wait:
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    len = select(sock + 1, &fds, 0, 0, &tv);
    switch (len) {
    case -1:
        if (errno == EINTR || errno == EINPROGRESS || errno == EAGAIN) {
            goto wait;
        }
        Tcl_AppendResult(interp, "select error: ", address, ":", port, " ", strerror(errno), 0);
        close(sock);
        return TCL_ERROR;

    case 0:
        if (--retries < 0) {
            goto resend;
        }
        Tcl_AppendResult(interp, "timeout", 0);
        close(sock);
        return TCL_ERROR;
    }
    if (FD_ISSET(sock, &fds)) {
        len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *) &sa, &salen);
        if (len > 0) {
            Tcl_AppendResult(interp, buf, 0);
        }
    }
    close(sock);
    return TCL_OK;
}

/*
 * sys_write
 *
 * Write data into file/socket descriptor
 *
 * Usage:
 *   sys_write fd data
 *
 * Returns:
 *  number of bytes written
*/
static int SysWrite(ClientData arg, Tcl_Interp * interp, int objc, Tcl_Obj * CONST objv[])
{
    unsigned char *data;
    int n, fd, left, sent = 0;
    
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "fd data");
        return TCL_ERROR;
    }

    fd = atoi(Tcl_GetString(objv[1]));
    data = Tcl_GetByteArrayFromObj(objv[2], &left);

    while (left > 0) {
        n = write(fd, data + sent, left);
        if (n <= 0) {
            break;
        }
        left -= n;
        sent += n;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(sent));
    return TCL_OK;
}

static int NsSysInterpInit(Tcl_Interp * interp, void *context)
{
    char *argv0;

    /* Syslog initialization */
    memset(&syslog_data, 0, sizeof(syslog_data));
    syslog_data.options = LOG_PID;
    syslog_data.facility = LOG_USER;
    if ((argv0 = (char *) Tcl_GetVar(interp, "argv0", TCL_GLOBAL_ONLY))) {
        strncpy(syslog_data.ident, argv0, sizeof(syslog_data.ident) - 1);
    }
    /* System commands */
    Tcl_CreateCommand(interp, "ns_syslog", SysLog, context, 0);
    Tcl_CreateCommand(interp, "ns_sysstatfs", SysStatFs, context, 0);
    Tcl_CreateObjCommand(interp, "ns_sysioctl", SysIoctl, context, 0);
    Tcl_CreateObjCommand(interp, "ns_sysv4l", SysV4L, context, 0);
    Tcl_CreateObjCommand(interp, "ns_sysxevent", SysXevent, context, 0);
    Tcl_CreateObjCommand(interp, "ns_sysudp", SysUdp, context, 0);
    Tcl_CreateObjCommand(interp, "ns_syswrite", SysWrite, context, 0);

    return 0;
}
