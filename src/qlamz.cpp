/* Copyright (c) 2012 Christian Krippendorf <Coding@Christian-Krippendorf.de>
 *
 * This file is part of qlamz.
 *
 * qlamz is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * qlamz is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * qlamz.  If not, see <http://www.gnu.org/licenses/>. */

#include "qlamz.h"
#include "ui_qlamz.h"

#include "config.h"
#include "Settings.h"
#include "About.h"
#include "Error.h"
#include "Track.h"
#include "TrackModel.h"
#include "TrackDownloader.h"
#include "CheckboxDelegate.h"
#include "ProgressDelegate.h"

#include <QCloseEvent>
#include <QList>
#include <QStringList>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QFileDialog>
#include <QSettings>
#include <QProcess>
#include <QXmlStreamReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkCookieJar>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QDomDocument>

#include <amz.h>

// Make a string from a value in preprocessor
#define STR(x) _STR(x)
#define _STR(x) #x

#define AMAZON_FILE_PATH SHARE_PATH/qlamz/amazon


qlamz::qlamz(QWidget *pParent)
    : QMainWindow(pParent),
    m_state(qlamz::Default),
    m_bCancel(false),
    m_iMaxDownloads(0),
    m_iLeftDownloads(0),
    m_iActualDownloadCount(0),
    m_iTotalDownloadCount(0),
    m_pTrackModel(new TrackModel()),
    m_pUi(new Ui::MainWindow()),
    m_pstrAmazonFilePath(new QString()),
    m_pSettings(new Settings(this)),
    m_pSettingsData(new QSettings()),
    m_pAmazonInfos(new QSettings(STR(AMAZON_FILE_PATH), QSettings::IniFormat)),
    m_pAbout(new About(this)),
    m_pError(new Error(this)),
    m_pErrors(new QStringList()),
    m_pRecentFiles(new QStringList()),
    m_pstrDestination(new QString()),
    m_pstrXmlData(new QString()),
    m_pstrOpenPath(new QString()),
    m_pAmz(new amz::amz()),
    m_pNetAccessManager(new QNetworkAccessManager(this))
{
    m_pUi->setupUi(this);

    // Init network access manager
    m_pNetAccessManager->setCookieJar(new QNetworkCookieJar(
        m_pNetAccessManager));

    // Set some window configs
    setWindowTitle(tr("qlamz"));

    // Set the model
    m_pUi->tableViewTracks->verticalHeader()->setResizeMode(QHeaderView::Fixed);
    m_pUi->tableViewTracks->horizontalHeader()->setResizeMode(QHeaderView::Fixed);
    m_pUi->tableViewTracks->verticalHeader()->setDefaultSectionSize(25);

    CheckboxDelegate *pCheckBoxDelegate = new CheckboxDelegate();

    m_pUi->tableViewTracks->setItemDelegateForColumn(0, pCheckBoxDelegate);
    m_pUi->tableViewTracks->setItemDelegateForColumn(1, new ProgressDelegate());
    m_pUi->tableViewTracks->setModel(m_pTrackModel);
    m_pUi->tableViewTracks->resizeColumnsToContents();
    m_pUi->tableViewTracks->hideColumn(1);

    // Build some connections
    connect(pCheckBoxDelegate, SIGNAL(editorClicked(int)), this,
        SLOT(checkBoxClicked(int)));

    connect(m_pSettings, SIGNAL(settingsSaved()), this,
        SLOT(loadSettings()));

    // Test for first run and set default settings.
    firstRun();

    // Load settings.
    loadSettings();

    // Set the settings data object
    m_pSettings->setSettingsData(m_pSettingsData);
}

qlamz::~qlamz()
{
    delete m_pstrOpenPath;
    delete m_pstrXmlData;
    delete m_pTrackModel;
    delete m_pUi;
    delete m_pSettings;
    delete m_pSettingsData;
    delete m_pAmazonInfos;
    delete m_pstrDestination;
    delete m_pError;
    delete m_pRecentFiles;
    delete m_pAbout;
    delete m_pAmz;
    delete m_pNetAccessManager;
}

QString qlamz::decryptAmazonFile(const QByteArray &amazonEncryptedContent)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(
        m_pAmz->decryptAmzData(const_cast<char *>(
        amazonEncryptedContent.data()), amazonEncryptedContent.size())));
}

void qlamz::checkBoxClicked(int iRow)
{
    Track *pTrack = m_pTrackModel->tracks().at(iRow);
    pTrack->setDownload(!pTrack->download());
}

void qlamz::amzDownloaded(const QString &strContent)
{
    QString strTmp;

    if (!isXml(strContent)) {
        strTmp = decryptAmazonFile(strContent.toUtf8());
        openAmazonFileFromString(strTmp);
    } else {
        openAmazonFileFromString(strContent);
    }
}

void qlamz::downloadFinish(Track *pTrack, QNetworkReply *pNetworkReply,
    TrackDownloader *pTrackDownloader)
{
    // Create the path if it does not exist.
    QString strDestinationPath = destinationPath(pTrack);
    QDir dir;

    dir.mkpath(strDestinationPath);

    if (m_pSettingsData->value("destination.numberPrefix", true).toBool()) {
        int iNumber = pTrack->number();
        QString strNumber;
        if (iNumber < 10) {
            strNumber = "0";
        }

        strNumber = strNumber + QString::number(iNumber);

        strDestinationPath = strDestinationPath + strNumber + " - ";
    }

    // Create the file and write all data.
    QFile file(strDestinationPath + pTrack->title().replace('/', QChar(0x2044)) + ".mp3");
    file.open(QIODevice::WriteOnly);
    file.write(pNetworkReply->readAll());
    file.close();

    // Increment the actual download progress.
    m_iActualDownloadCount++;

    // Update the window title.
    short sTotalPercentage = (short) ((double) m_iActualDownloadCount /
        (double) m_iTotalDownloadCount * 100);
    setWindowTitle(tr("qlamz - Total: %1%").arg(sTotalPercentage));

    // If there are files waiting for download, do so.
    if (m_trackList.size() > 0) {
        Track *pNextTrack = m_trackList.takeFirst();
        pTrackDownloader->startDownload(pNextTrack);
        m_pUi->tableViewTracks->update();
    } else {
        // Test if all downloader are not running.
        bool bDownloaderRunning = false;
        for (int i = 0; i < m_trackDownloaderList.size(); i++) {
            TrackDownloader *pTrackDownloaderTmp = m_trackDownloaderList.at(i);

            if (pTrackDownloader != pTrackDownloaderTmp &&
                pTrackDownloaderTmp->isRunning()) {
                bDownloaderRunning = true;
                break;
            }
        }

        // If no other downloader is running... reset the ui.
        if (!bDownloaderRunning) {
            m_state = qlamz::Default;
            updateUiState();
        }

        // Display the error dialog, if any errors occured.
        if (m_pErrors->size() > 0) {
            m_pError->exec(*m_pErrors);
        }
    }
}

void qlamz::downloadUpdate(Track *pTrack, qint64 iRecieved, qint64 iTotal,
    QNetworkReply *pNetworkReply)
{
    pTrack->setDownloadPercentage((short) ((double) iRecieved /
        (double) iTotal * 100));

    m_pUi->tableViewTracks->reset();

    short sTotalPercentage = (short) ((double) m_iActualDownloadCount /
        (double) m_iTotalDownloadCount * 100);

    setWindowTitle(tr("qlamz - Total: %1%").arg(sTotalPercentage));
}

void qlamz::downloadError(int iCode, const QString &strMessage, Track *pTrack)
{
    m_pErrors->append("Error downloading pTrack: " + pTrack->title() +
        "\n" + strMessage + "\n");

    qDebug() << "Error downloading pTrack: " + pTrack->title() + "\n" +
        strMessage + "\n";
}

void qlamz::firstRun()
{
    // Test if we run the first time.
    if (m_pSettingsData->value("firstRun", true).toBool()) {
        qDebug() << "First run of application -> set default settings";
        // Set first time run to false.
        m_pSettingsData->setValue("firstRun", false);
        // Set default settings.
        setDefaultSettings();
    }
}

void qlamz::setDefaultSettings()
{
    m_pSettingsData->setValue("recentFiles", QStringList());
    m_pSettingsData->setValue("maxDownloads", 1);
    m_pSettingsData->setValue("windowSize", QSize(400, 500));
    m_pSettingsData->setValue("openPath", QDir::homePath());
    m_pSettingsData->setValue("destination.dir", QDir::homePath());
    m_pSettingsData->setValue("amazon.tld", "com");
    m_pSettingsData->setValue("destination.format", QString("${albumPrimaryArtist}/${album}"));

    m_pSettingsData->sync();
}

void qlamz::saveSettings()
{
    m_pSettingsData->setValue("recentFiles", *m_pRecentFiles);
    m_pSettingsData->setValue("maxDownloads", m_iMaxDownloads);
    m_pSettingsData->setValue("windowSize", size());
    m_pSettingsData->setValue("openPath", *m_pstrOpenPath);
}

void qlamz::loadSettings()
{
    // Load the recent files from the settings.
    *m_pRecentFiles = m_pSettingsData->value("recentFiles").toStringList();

    // Load the maximum simulatanouse downloads.
    m_iMaxDownloads = m_pSettingsData->value("maxDownloads").toInt();
    updateTrackDownloader();

    // Load and set the window size and position.
    resize(m_pSettingsData->value("windowSize").toSize());

    // Load the standard path for open filedialogs.
    *m_pstrOpenPath = m_pSettingsData->value("openPath").toString();

    // Init the TrackDownloader list.
    int iMaxDownloadDiff = m_iMaxDownloads - m_trackDownloaderList.size();

    if (iMaxDownloadDiff != 0) {
        if (iMaxDownloadDiff > 0) {
            // Create new TrackDownlader object, until we got the maximum
            // number.
            while (iMaxDownloadDiff-- > 0) {
                TrackDownloader *pTrackDownloader = new TrackDownloader(
                    m_pNetAccessManager, this);

                m_trackDownloaderList.append(pTrackDownloader);

                qDebug() << "Create TrackDownloader";

                // Build some connections.
                connect(pTrackDownloader, SIGNAL(finish(Track*, QNetworkReply*,
                    TrackDownloader*)), this, SLOT(downloadFinish(Track*,
                    QNetworkReply*, TrackDownloader*)));

                connect(pTrackDownloader, SIGNAL(update(Track*, qint64, qint64,
                    QNetworkReply*)), this, SLOT(downloadUpdate(Track*, qint64,
                    qint64, QNetworkReply *)));

                connect(pTrackDownloader, SIGNAL(error(int, const QString&,
                    Track*)), this, SLOT(downloadError(int, const QString&,
                    Track*)));
            }
        } else {
            // Delete TrackDownloader objects, until we got the maximum number.
            while (m_trackDownloaderList.size() > m_iMaxDownloads) {
                delete m_trackDownloaderList.takeFirst();
            }
        }
    }

    updateRecentFiles();
}

void qlamz::updateTrackDownloader()
{
    // Init the TrackDownloader list.
    int iMaxDownloadDiff = m_iMaxDownloads - m_trackDownloaderList.size();

    if (iMaxDownloadDiff != 0) {
        if (iMaxDownloadDiff > 0) {
            // Create new TrackDownlader object, until we got the maximum
            // number.
            while (iMaxDownloadDiff-- > 0) {
                TrackDownloader *pTrackDownloader = new TrackDownloader(
                    m_pNetAccessManager, this);

                m_trackDownloaderList.append(pTrackDownloader);

                qDebug() << "Create Track Downloader";

                // Build some connections.
                connect(pTrackDownloader, SIGNAL(finish(Track*, QNetworkReply*,
                    TrackDownloader*)), this, SLOT(downloadFinish(Track*,
                    QNetworkReply*, TrackDownloader*)));

                connect(pTrackDownloader, SIGNAL(update(Track*, qint64, qint64,
                    QNetworkReply*)), this, SLOT(downloadUpdate(Track*, qint64,
                    qint64, QNetworkReply *)));

                connect(pTrackDownloader, SIGNAL(error(int, const QString&,
                    Track*)), this, SLOT(downloadError(int, const QString&,
                    Track*)));
            }
        } else {
            // Delete TrackDownloader objects, until we got the maximum number.
            while (m_trackDownloaderList.size() > m_iMaxDownloads) {
                qDebug() << "Delete Track Downloader";
                delete m_trackDownloaderList.takeFirst();
            }
        }
    }
}

void qlamz::recentFileTriggered()
{
    QAction *action = (QAction *) sender();
    openAmazonFile(action->text());
}

void qlamz::aboutQt()
{
    QApplication::aboutQt();
}

void qlamz::showErrorLog()
{
    if (m_pErrors->size() > 0) {
        m_pError->exec(*m_pErrors);
    } else {
        QMessageBox::information(this, tr("Information"),
            tr("No error messages to display"), QMessageBox::Ok);
    }
}

void qlamz::showXMLContent()
{
    if (m_pstrXmlData->size() > 0) {
        m_pError->exec(*m_pstrXmlData);
    } else {
        QMessageBox::information(this, tr("Information"),
            tr("No xml content to display"), QMessageBox::Ok);
    }
}

void qlamz::openAmazonStore(const QString &strUrl)
{
    // Url given to display?
    bool bUrl = (strUrl.size() > 0) ? true : false;

    if (bUrl) {
        // Show the url in the external web browser.
        QDesktopServices::openUrl(strUrl);
    } else {
        // Get the amazon url from the data file.
        QString strAmazonUrl = m_pAmazonInfos->value("amazon.store.url." +
            m_pSettingsData->value(QString("amazon.tld"), QString()).toString(),
            QString()).toString();

        QDesktopServices::openUrl(strAmazonUrl);
    }
}

bool qlamz::isXml(const QString &strContent)
{
    QDomDocument doc;
    if (!doc.setContent(strContent)) {
        return false;
    }

    return true;
}

void qlamz::openAmazonFileFromString(const QString &strContent)
{
    *m_pstrXmlData = strContent;

    QList<Track *> tracks = readTracksFromXml(strContent);

    qDebug() << __func__ << ": Number of Tracks: " << tracks.size();

    if (tracks.size() < 1) {
        QMessageBox::information(this, tr("Information"),
            tr("The file you try to open has no tracks. The file will be "
            "unloaded."));
        reset();
        return;
    }

    m_pTrackModel->removeTracks();
    m_pTrackModel->appendTracks(tracks);
    m_pUi->tableViewTracks->resizeColumnsToContents();

    // Set state to default.
    m_state = qlamz::Default;
    updateUiState();

    m_pUi->tableViewTracks->hideColumn(1);
    m_pUi->actionDeselectAll->setEnabled(true);
    m_pUi->actionSelectAll->setEnabled(true);
}

void qlamz::openAmazonFile(const QString &strAmazonFileArg)
{
    QString strAmazonFile;

    if (strAmazonFileArg.size() > 0) {
        strAmazonFile = strAmazonFileArg;
    } else {
        strAmazonFile = QFileDialog::getOpenFileName(this,
            tr("Open Amazon File"), *m_pstrOpenPath, tr("Amazon (*.amz)"));
    }

    if (strAmazonFile.size() > 0) {
        QFileInfo pathInfo(strAmazonFile);

        if (!pathInfo.exists()) {
            QMessageBox::information(this, tr("Information"),
                tr("The file you try to open does not exist."));
            reset();
            return;
        }

        *m_pstrOpenPath = pathInfo.path();
        *m_pstrAmazonFilePath = strAmazonFile;
        setWindowTitle("qlamz - " + strAmazonFile);
    } else {
        return;
    }

    // Set the recent files menu.
    while (m_pRecentFiles->indexOf(strAmazonFile) > -1) {
        m_pRecentFiles->takeAt(m_pRecentFiles->indexOf(strAmazonFile));
    }

    m_pRecentFiles->insert(0, strAmazonFile);

    // Remove the last entry, when tere are more than 10.
    if (m_pRecentFiles->size() > 10) {
        m_pRecentFiles->takeLast();
    }

    updateRecentFiles();

    // Create a temporary file from the xml output of the amazon file.
    QString strXmlData = getXmlFromFile(*m_pstrAmazonFilePath);

    openAmazonFileFromString(strXmlData);
}

void qlamz::updateDownloadButton()
{
    if (m_pstrAmazonFilePath->length() > 0) {
        m_pUi->buttonDownload->setEnabled(true);
    } else {
        m_pUi->buttonDownload->setEnabled(false);
    }
}

void qlamz::startDownload()
{
    // Set the actual state.
    m_state = qlamz::Download;
    updateUiState();

    // Display the download progress column.
    m_pUi->tableViewTracks->showColumn(1);

    // Add all tracks that are checked to the m_trackList.
    QList<Track *> trackList = m_pTrackModel->tracks();
    m_trackList.clear();

    for (int i = 0; i < trackList.size(); i++) {
        if (trackList.at(i)->download()) {
            m_trackList.append(trackList.at(i));
        }
    }

    m_iActualDownloadCount = 0;
    m_iTotalDownloadCount = m_trackList.size();

    // Start all TrackDownloader with downloading a file.
    for (int i = 0; i < m_trackDownloaderList.size(); i++) {
        if (m_trackList.size() <= 0) {
            break;
        }

        Track *pTrack = m_trackList.takeFirst();
        m_trackDownloaderList.at(i)->startDownload(pTrack);
    }

    m_pErrors->clear();
}

void qlamz::cancelDownload()
{
    int iReturn = QMessageBox::question(this, tr("Cancel download"),
        tr("Are you sure canceling the download progress?"),
        QMessageBox::Yes | QMessageBox::No);

    if (iReturn != QMessageBox::Yes) {
        return;
    }

    m_bCancel = true;

    for (int i = 0; i < m_trackDownloaderList.size(); i++) {
        m_trackDownloaderList.at(i)->abort();
    }

    m_state = qlamz::Default;
    updateUiState();
}

void qlamz::settings()
{
    m_pSettings->exec();
}

void qlamz::about()
{
    m_pAbout->exec();
}

void qlamz::selectAll()
{
    // Close any opened editor.
    m_pUi->tableViewTracks->closePersistentEditor(
        m_pUi->tableViewTracks->currentIndex());

    QList<Track *> tracks = m_pTrackModel->tracks();

    for (int i = 0; i < tracks.size(); i++) {
        tracks.at(i)->setDownload(true);
    }

    m_pUi->tableViewTracks->reset();
}

void qlamz::deselectAll()
{
    // Close any opened editor.
    m_pUi->tableViewTracks->closePersistentEditor(
        m_pUi->tableViewTracks->currentIndex());

    QList<Track *> tracks = m_pTrackModel->tracks();

    for (int i = 0; i < tracks.size(); i++) {
        tracks.at(i)->setDownload(false);
    }

    m_pUi->tableViewTracks->reset();
}

void qlamz::cookieAmazonDe()
{
    QString strCookieUrl = m_pAmazonInfos->value("amazon.cookie.url.de",
            QString()).toString();

    openAmazonStore(strCookieUrl);
}

void qlamz::closeEvent(QCloseEvent *pEvent)
{
    // Don't quit until we are downloading.
    if (m_state == qlamz::Download) {
        QMessageBox::information(this, tr("Information"),
            tr("Still downloading content. Please wait until it has "
            "finished or abort the process."), QMessageBox::Ok);

        pEvent->ignore();
        return;
    }

    // Save settings for closing.
    saveSettings();

    // Close the window.
    QMainWindow::closeEvent(pEvent);
}

void qlamz::updateUiState()
{
    switch (m_state) {
    case qlamz::Empty:
        m_pUi->buttonCancel->setEnabled(false);

        // Sepcial handling for the download button.
        updateDownloadButton();

        m_pUi->tableViewTracks->setEnabled(false);

        m_pUi->actionDeselectAll->setEnabled(false);
        m_pUi->actionSelectAll->setEnabled(false);
        m_pUi->actionSettings->setEnabled(true);
        break;
    case qlamz::Default:
        m_pUi->buttonQuit->setEnabled(true);
        m_pUi->buttonCancel->setEnabled(false);

        // Sepcial handling for the download button.
        updateDownloadButton();

        m_pUi->tableViewTracks->setEnabled(true);

        m_pUi->actionDeselectAll->setEnabled(true);
        m_pUi->actionSelectAll->setEnabled(true);
        m_pUi->actionSettings->setEnabled(true);

        break;
    case qlamz::Download:
        m_pUi->buttonQuit->setEnabled(false);
        m_pUi->buttonCancel->setEnabled(true);
        m_pUi->buttonDownload->setEnabled(false);

        m_pUi->tableViewTracks->setEnabled(false);

        m_pUi->actionDeselectAll->setEnabled(false);
        m_pUi->actionSelectAll->setEnabled(false);
        m_pUi->actionSettings->setEnabled(false);

        break;
    default:
        qDebug() << __func__ << ": Unknown state";
    }
}

void qlamz::reset()
{
    // Empty all strings.
    *m_pstrXmlData = "";
    *m_pstrAmazonFilePath = "";

    // Remove all tracks.
    m_pTrackModel->removeTracks();

    // Update table.
    m_pUi->tableViewTracks->reset();

    // Set window title.
    setWindowTitle(tr("qlamz"));

    // Change the ui state.
    m_state = qlamz::Empty;
    updateUiState();
}

void qlamz::updateRecentFiles()
{
    // Delete all old actions.
    m_pUi->menuRecentFiles->clear();

    for (int i = 0; i < m_pRecentFiles->size(); i++) {
        QAction *action = new QAction(m_pRecentFiles->at(i),
            m_pUi->menuRecentFiles);

        m_pUi->menuRecentFiles->addAction(action);

        connect(action, SIGNAL(triggered()), this, SLOT(recentFileTriggered()));
    }
}

QList<Track *> qlamz::readTracksFromXml(const QString &strData)
{
    QList<Track *> tracks;

    QXmlStreamReader xmlReader(strData);

    while (!xmlReader.atEnd()) {
        xmlReader.readNextStartElement();

        // If finding <track> get the properties.
        if (xmlReader.name() == "track") {
            Track *pTrack = new Track();

            while (!(xmlReader.tokenType() == QXmlStreamReader::EndElement &&
                xmlReader.name() == "track")) {

                xmlReader.readNext();

                // We just go one, when we get a starting element.
                if (xmlReader.tokenType() == QXmlStreamReader::StartElement) {
                    if (xmlReader.name() == "location") {
                        pTrack->setLocation(xmlReader.readElementText());
                    } else if (xmlReader.name() == "album") {
                        pTrack->setAlbum(xmlReader.readElementText());
                    } else if (xmlReader.name() == "creator") {
                        pTrack->setCreator(xmlReader.readElementText());
                    } else if (xmlReader.name() == "title") {
                        QString strTitle = xmlReader.readElementText();
                        pTrack->setTitle(strTitle);
                    } else if (xmlReader.name() == "trackNum") {
                        pTrack->setNumber(xmlReader.readElementText().toInt());
                    } else if (xmlReader.name() == "meta") {
                        // Read in all meta infos identified by the rel var.
                        QXmlStreamAttributes attributes =
                            xmlReader.attributes();

                        if (attributes.hasAttribute("rel")) {
                            QString rel = attributes.value("rel").toString()
                                .split("/").last();

                            // Interpret rel
                            if (rel == "trackType") {
                                pTrack->setTrackType(xmlReader
                                    .readElementText());
                            } else if (rel == "primaryGenre") {
                                pTrack->setPrimaryGenre(xmlReader
                                    .readElementText());
                            } else if (rel == "albumPrimaryArtist") {
                                pTrack->setAlbumPrimaryArtist(
                                    xmlReader.readElementText());
                            }
                        }
                    } else {
                        xmlReader.skipCurrentElement();
                    }
                }
            }

            // Add the track to the track list.
            tracks.append(pTrack);
        }
    }

    if (xmlReader.hasError()) {
        qDebug() << __func__ << ": XmlReaderError: " << xmlReader.errorString();
    }

    return tracks;
}

QString qlamz::getXmlFromFile(const QString &strAmazonFilePath)
{
    QFile file(strAmazonFilePath);
    file.open(QIODevice::ReadOnly);

    QByteArray tmpData = file.readAll().replace('\n', "");
    file.close();

    if (isXml(tmpData)) {
        return QString::fromUtf8(tmpData);
    } else {
        return decryptAmazonFile(tmpData);
    }
}

QString qlamz::destinationPath(const Track * const pTrack) const
{
    // Load the information about the destination and build the destination path.
    QString strDestination = m_pSettingsData->value("destination.dir").toString();
    QString strDestinationFormat = m_pSettingsData->value("destination.format").toString();

    // Replace the creator and the album.
    strDestinationFormat.replace(QString("${creator}"), pTrack->creator());
    strDestinationFormat.replace(QString("${album}"), pTrack->album());
    strDestinationFormat.replace(QString("${albumPrimaryArtist}"), pTrack->albumPrimaryArtist());

    return strDestination + "/" + strDestinationFormat + "/";
}
