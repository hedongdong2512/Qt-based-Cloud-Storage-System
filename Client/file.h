#ifndef FILE_H
#define FILE_H

#include "protocol.h"
#include "sharefile.h"

#include <QListWidget>
#include <QWidget>

namespace Ui {
class File;
}

class File : public QWidget
{
    Q_OBJECT

public:
    explicit File(QWidget *parent = nullptr);
    ~File();
    QString m_strCurPath;
    QString m_strUserPath;
    QString m_strMvFileName;
    QString m_strMvFilePath;
    ShareFile* m_pShareFile;
    QString m_strUploadFilePath;
    qint64 m_iUploadOffset = 0;     //断点续传偏移量（服务器已收到的字节数）
    QString m_strDownloadSavePath;  //下载文件保存到本地的路径
    void updateFileList(QList<FileInfo*> pFileList);
    void uploadFile();
    QList<FileInfo*> m_pFileInfoList;
    void flushFile();

private slots:
    void on_mkdir_PB_clicked();

    void on_flush_PB_clicked();

    void on_deldir_PB_clicked();

    void on_rename_PB_clicked();

    void on_return_PB_clicked();

    void on_listWidget_itemDoubleClicked(QListWidgetItem *item);

    void on_mv_PB_clicked();

    void on_share_PB_clicked();

    void on_upload_PB_clicked();

    void on_download_PB_clicked();

private:
    Ui::File *ui;
};

#endif // FILE_H
