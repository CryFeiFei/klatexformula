/***************************************************************************
 *   file klfbackend.h
 *   This file is part of the KLatexFormula Project.
 *   Copyright (C) 2007 by Philippe Faist
 *   philippe.faist@bluewin.ch
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
/* $Id$ */

#ifndef KLFBACKEND_H
#define KLFBACKEND_H

#include <klfdefs.h>

#include <qstring.h>
#ifdef KLFBACKEND_QT4
#include <QByteArray>
#else
#include <qmemarray.h>
#endif
#include <qimage.h>
#include <qmutex.h>



//! Definition of class \ref KLFBackend
/** \file
 * This file defines the \ref KLFBackend class, which is the base engine providing
 * our core functionality of transforming LaTeX code into graphics.
 */


/** \mainpage
 * <center>
 * <div style="width: 60%; text-align: justify; font-weight: bold">This documentation is the API
 * documentation for the KLatexFormula library backend that
 * you may want to use in your programs. It is a GPL-licensed library based on QT 3 or QT 4 that
 * converts a LaTeX equation given as text into graphics, specifically PNG, EPS or PDF.<br><br>
 * All the core functionality is based in the class \ref KLFBackend .<br><br>
 * This library will compile indifferently on QT 3 and QT 4 with the same source code.
 * The base API is the same, although QT3-specific functions were removed in QT 4-compatible
 * code (cf. KLFBlockProcess::startProcess(QStringList cmd, QCString str, QStringList env = QStringList()) ).
 * To compile with QT4, you need to add KLFBACKEND_QT4 to your defines, ie. pass option -DKLFBACKEND_QT4 to gcc.<br><br>
 * On QT 3, this library has been tested to work in non-GUI applications (ie. FALSE in QApplication constructor).
 *</div></center>
 */

//! No Error.
#define KLFERR_NOERROR 0

//! No LaTeX formula is specified (empty string)
#define KLFERR_MISSINGLATEXFORMULA -1
//! The \c "..." is missing in math mode string
#define KLFERR_MISSINGMATHMODETHREEDOTS -2
//! Error while opening .tex file for writing
#define KLFERR_TEXWRITEFAIL -3
//! Error while launching the given \c latex program
#define KLFERR_NOLATEXPROG -4
//! \c latex program did not exit properly (program killed) (see also \ref KLFERR_PROGERR_LATEX)
#define KLFERR_LATEXNONORMALEXIT -5
//! No .dvi file appeared after runnig \c latex program
#define KLFERR_NODVIFILE -6
//! Error while launching the given \c dvips program
#define KLFERR_NODVIPSPROG -7
//! \c dvips program did not exit properly (program killed) (see also \ref KLFERR_PROGERR_DVIPS)
#define KLFERR_DVIPSNONORMALEXIT -8
//! no .eps file appeared after running \c dvips program
#define KLFERR_NOEPSFILE -9
//! Error while opening .eps file for reading
#define KLFERR_EPSREADFAIL -10
//! Error while searching file for <tt>%%BoundingBox</tt> instruction in EPS
#define KLFERR_NOEPSBBOX -11
//! Error while parsing value for <tt>%%BoundingBox</tt> instruction in EPS
#define KLFERR_BADEPSBBOX -12
//! Error while opening <tt>...-good.eps</tt> file for writing
#define KLFERR_EPSWRITEFAIL -13
//! Error while launching the given \c gs program
#define KLFERR_NOGSPROG -14
//! \c gs program did not exit properly (program killed) (see also \ref KLFERR_PROGERR_GS)
#define KLFERR_GSNONORMALEXIT -15
//! No .png file appeared after running \c gs program
#define KLFERR_NOPNGFILE -16
//! Error while opening .png file for reading
#define KLFERR_PNGREADFAIL -17
//! Error while launching the given \c epstopdf program (if given)
#define KLFERR_NOEPSTOPDFPROG -18
//! \c epstopdf program did not exit properly (program killed) (see also \ref KLFERR_PROGERR_EPSTOPDF)
#define KLFERR_EPSTOPDFNONORMALEXIT -19
//! No .pdf file appeared after running \c epstopdf program
#define KLFERR_NOPDFFILE -20
//! Error while opening .pdf file for reading
#define KLFERR_PDFREADFAIL -21

//! \c latex exited with a non-zero status
#define KLFERR_PROGERR_LATEX 1
//! \c dvips exited with a non-zero status
#define KLFERR_PROGERR_DVIPS 2
//! \c gs exited with a non-zero status
#define KLFERR_PROGERR_GS 3
//! \c epstopdf exited with non-zero status (if \c epstopdf is to be used)
#define KLFERR_PROGERR_EPSTOPDF 4



//! The main engine for KLatexFormula
/** The main engine for KLatexFormula, providing core functionality
 * of transforming LaTeX code into graphics.
 *
 * Don't instanciate this class, use the static function
 * \ref KLFBackend::getLatexFormula() to do all the processing.
 *
 * \author Philippe Faist &lt;philippe.faist@bluewin.ch&gt;
 */
class KLF_EXPORT KLFBackend
{
public:

  //! General settings for KLFBackend::getLatexFormula()
  /** Some global settings to pass on when calling getLatexFormula(). In this struct you specify
   * some system settings, like a temp directory and some paths
   *
   * \note the \c klfclspath field was removed, because we no longer use klatexformula.cls.
   * */
  struct klfSettings {
    /** A temporary directory in which we have write access, e.g. <tt>/tmp/</tt> */
    QString tempdir;
    /** the latex executable, path incl. if not in $PATH */
    QString latexexec;
    /** the dvips executable, path incl. if not in $PATH */
    QString dvipsexec;
    /** the gs executable, path incl. if not in $PATH */
    QString gsexec;
    /** the epstopdf executable, path incl. if not in $PATH. This isn't mandatory to get PNG so
     * you may leave this to Null or Empty string to instruct getLatexFormula() to NOT attempt to
     * generate PDF. If, though, you do specify an epstopdf executable here, epstopdf errors will
     * be reported as real errors. */
    QString epstopdfexec;

    /** The number of postscript points to add to top side of the resulting EPS boundingbox */
    int tborderoffset;
    /** The number of postscript points to add to right side of the resulting EPS boundingbox */
    int rborderoffset;
    /** The number of postscript points to add to bottom side of the resulting EPS boundingbox */
    int bborderoffset;
    /** The number of postscript points to add to left side of the resulting EPS boundingbox */
    int lborderoffset;
  };

  //! Specific input to KLFBackend::getLatexFormula()
  /** This struct descibes the input of getLatexFormula(), ie. the LaTeX code, the mathmode to use,
   * the dpi for rendering png, colors etc. */
  struct klfInput {
    /** A default constructor assigning default values to all fields. */
    klfInput() : fg_color(0x00), bg_color(0xffffffff), dpi(600), smoothFactor(1) { }
    /** The latex code to render */
    QString latex;
    /** The mathmode to use. You may pass an arbitrary string containing '...' . '...' will be replaced
     * by the latex code. Examples are:
     * \li <tt>\\[ ... \\]</tt>
     * \li <tt>\$ ... \$</tt>
     */
    QString mathmode;
    /** The LaTeX preample, ie the code that appears after '\\documentclass{...}' and
     * before '\\begin{document}' */
    QString preamble;
    /** The foreground color to use, in format given by <tt>qRgb(r, g, b)</tt>.
     * You may not specify an alpha value here, it will be ignored. */
    unsigned long fg_color;
    /** The background color to use, in format given by <tt>qRgba(r, g, b, alpha)</tt>.
     * \warning background alpha value can only be 0 or 255, not any arbitrary value. Any non-zero
     *   value will be considered as 255.
     * \warning (E)PS and PDF formats can't handle transparency.
     */
    unsigned long bg_color;
    /** The dots per inch resolution of the resulting image. This is not directly passed to the
     * <tt>-r</tt> option of the \c gs program, see \ref smoothFactor. */
    int dpi;
    /** A smoothening factor. Can be seen as an extra anti-aliasing feature. The image of the latex
     * equation is rendered with \c gs program using a DPI value of <tt>dpi*smoothFactor</tt> and
     * then the image is scaled down by a factor of \c smoothFactor, such that the resulting image
     * has a resolution of \c dpi. */
    int smoothFactor;
  };

  //! KLFBackend::getLatexFormula() result
  /** This struct contains data that is returned from getLatexFormula(). This includes error handling
   * information, the resulting image (as a QImage) as well as data for PNG, (E)PS and PDF files */
  struct klfOutput {
    /** \brief A code describing the status of the request.
     *
     * A zero value means success for everything. A positive value means that a program (latex, dvips,
     * ...) returned a non-zero exit code. A negative status indicates an other error.
     *
     * \c status will be exactly one of the KLFERR_* constants.
     *
     * In every case where status is non-zero, a suitable human-readable error string will be provided in
     * the \ref errorstr field. If status is zero, \ref errorstr will be empty.  */
    int status;
    /** \brief An explicit error string.
     *
     * If \ref status is positive (ie. latex/dvips/gs/epstopdf error) then this text is HTML-formatted
     * suitable for a QTextBrowser. Otherwise, the message is a simple plain text sentence. It contains
     * an empty (actually null) string if status is zero.
     *
     * This string is Qt-Translated with QObject::tr() using \c "KLFBackend" as comment. */
    QString errorstr;

    /** The actual resulting image. */
    QImage result;

    /** the data for a png file (exact \c gs output content)
     *
     * \note This PNG image is not downscaled to the correct DPI. it is still at DPI*smoothFactor
     *   size. The resulting image \c result, is, however of correct size. */
    QByteArray pngdata;
    /** data for an (eps-)postscript file */
    QByteArray epsdata;
    /** data for a pdf file, if \ref klfSettings::epstopdfexec is non-empty. */
    QByteArray pdfdata;
  };

  /** The call to process everything.
   *
   * Pass on a valid \ref klfInput input object, as well as a \ref klfSettings
   * object filled with your input and settings, and you will get output in \ref klfOutput.
   *
   * If an error occurs, klfOutput::status is non-zero and klfOutput::errorstr contains an explicit
   * error in human-readable form. The latter is Qt-Translated with QObject::tr() with "KLFBackend"
   * comment.
   *
   * Usage example:
   * \code
   *   ...
   *   // these could have been declared at some more global scope
   *   KLFBackend::klfSettings settings;
   *   settings.tempdir = "/tmp";
   *   settings.latexexec = "latex";
   *   settings.dvipsexec = "dvips";
   *   settings.gsexec = "gs";
   *   settings.epstopdfexec = "epstopdf";
   *   settings.lborderoffset = 1;
   *   settings.tborderoffset = 1;
   *   settings.rborderoffset = 1;
   *   settings.bborderoffset = 1;
   *
   *   KLFBackend::klfInput input;
   *   input.latex = "\\int_{\\Sigma}\\!(\\vec{\\nabla}\\times\\vec u)\\,d\\vec S ="
   *     " \\oint_C \\vec{u}\\cdot d\\vec r";
   *   input.mathmode = "\\[ ... \\]";
   *   input.preamble = "\\usepackage{somerequiredpackage}\n";
   *   input.fg_color = qRgb(255, 128, 128); // pink
   *   input.bg_color = qRgba(0, 0, 80, 255); // dark blue, opaque
   *   input.dpi = 300;
   *
   *   KLFBackend::klfOutput out = KLFBackend::getLatexFormula(input, settings);
   *
   *   if (out.status != 0) {
   *     // an error occurred. an appropriate error string is in out.errorstr
   *     display_error_to_user(out.errorstr);
   *     return;
   *   }
   *
   *   myLabel->setPixmap(QPixmap(out.result));
   *
   *   // write contents of 'out.pdfdata' to a file to get a PDF file (for example)
   *   {
   *     QFile fpdf(fname);
   *     fpdf.open(IO_WriteOnly | IO_Raw);
   *     fpdf.writeBlock(out.pdfdata);
   *   }
   *   ...
   * \endcode
   *
   * \note This function is safe for threads; it locks a mutex at the beginning and unlocks
   *   it at the end; so if a call to this function is issued while a first call is already
   *   being processed in another thread, the second waits for the first call to finish.
   */
  static klfOutput getLatexFormula(const klfInput& in, const klfSettings& settings);

  /** \brief Save the output to image file
   *
   * This function can be used to write output obtained with the \ref getLatexFormula() function,
   * to a file named \c fileName with format \c format.
   *
   * \param output the data to save (e.g. as returned by \ref getLatexFormula() )
   * \param fileName the file name to save to. If empty or equal to \c "-" then standard output is used.
   * \param format the format to use to save to fileName
   *
   * If \c format is an empty string, then format is guessed from filename extension; if no extension is
   * found then format defaults to PNG.
   *
   * \c fileName 's extension is NOT adjusted if it does not match an explicitely given format, for
   * example
   * \code saveOutputToFile(output, "myfile.jpg", "PDF"); \endcode
   * will output PDF data to the file \c "myfile.jpg".
   *
   * \return TRUE if success or FALSE if failure.
   *
   * qWarning()s are emitted in case of failure.
   */
  static bool saveOutputToFile(const klfOutput& output, const QString& fileName,
			       const QString& format = QString());

  /** \brief Detects the system settings and stores the guessed values in \c settings.
   *
   * This function tries to find the latex, dvips, gs, and epstopdf in standard locations on the
   * current platform.
   *
   * The temporary directory is set to the system temporary directory.
   */
  static bool detectSettings(klfSettings *settings, const QString& extraPath = QString());


private:
  KLFBackend();

  friend struct cleanup_caller;
  static void cleanup(QString tempfname);

  static QMutex __mutex;
};


/** Compare two inputs for equality */
bool KLF_EXPORT operator==(const KLFBackend::klfInput& a, const KLFBackend::klfInput& b);


#endif
