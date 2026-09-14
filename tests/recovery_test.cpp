#include "recovery.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <cstdlib>
#include <iostream>
static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--interrupt") {
        RecoveryFile file(QString::fromLocal8Bit(argv[2]));
        QString error;
        require(file.write(RecoverySnapshot{},error), "child snapshot");
        std::_Exit(0); // Leave both the snapshot and lock behind, as a crash would.
    }
    QTemporaryDir directory;
    RecoverySnapshot snapshot;
    require(snapshot.document.addPolyline({{0,0},{4096,0},{4096,4096},{0,4096}},true), "outer room");
    require(snapshot.document.addPolyline({{1024,1024},{3072,1024},{3072,3072},{1024,3072}},true), "inner room");
    QString error;
    require(snapshot.document.removeSectors({1},error), "void hole");
    const auto sprite = snapshot.document.addSprite({12.25,18.75}); // No texture yet: not exportable.
    auto values = snapshot.document.sprites()[sprite];
    values.lotag = 65535; values.owner = -1; values.z = 123.5;
    snapshot.document.setSprite(sprite,values);
    auto sector = snapshot.document.sectors()[0];
    sector.ceilingheinum = -256; sector.ceilingstat = 2; sector.floorTexture = 142;
    snapshot.document.setSector(0,sector);
    snapshot.document.setPlayerStartPosition({-9000,0}); // Also not exportable.
    snapshot.drawingPoints = {{4000.5,1000.25},{5000,1000}};
    snapshot.sourceFilename = directory.filePath("original.map");
    snapshot.timestamp = QDateTime::currentDateTimeUtc();
    const auto path = RecoveryFile::newPath(directory.path());
    {
        RecoveryFile file(path);
        require(file.locked() && file.write(snapshot,error), "write snapshot");
        RecoveryFile competing(path);
        require(!competing.locked() && !competing.remove() && !competing.write(snapshot,error), "active session protected");
        require(RecoveryFile::candidates(directory.path()).contains(path), "discover snapshot");
    }
    RecoveryFile abandoned(path);
    RecoverySnapshot recovered;
    require(abandoned.locked() && abandoned.read(recovered,error), "recover abandoned snapshot");
    require(recovered.document == snapshot.document && recovered.drawingPoints == snapshot.drawingPoints
            && recovered.sourceFilename == snapshot.sourceFilename && recovered.timestamp == snapshot.timestamp,
            "lossless recovery of invalid map, holes, fields and unfinished drawing");
    const auto previous = recovered.document;
    auto data = RecoveryCodec::encode(snapshot);
    data[data.size()-1] = char(data.back() ^ 1);
    require(!RecoveryCodec::decode(data,recovered,error) && recovered.document == previous, "corruption rejected transactionally");
    require(!RecoveryCodec::decode(data.left(10),recovered,error), "truncation rejected");
    snapshot.document.setPlayerStartAngle(123.25);
    require(abandoned.write(snapshot,error) && abandoned.read(recovered,error)
            && recovered.document == snapshot.document, "replace snapshot atomically");
    require(abandoned.remove() && !QFile::exists(path), "explicit cleanup");
    require(!QFile::exists(snapshot.sourceFilename), "recovery never writes source map");
    const auto crashPath = RecoveryFile::newPath(directory.path());
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--interrupt", crashPath});
    require(child.waitForFinished(10000) && child.exitCode() == 0, "interrupt child process");
    RecoveryFile crashed(crashPath);
    require(crashed.locked() && crashed.read(recovered,error), "dead process lock permits recovery");
    require(crashed.remove(), "remove recovered crash snapshot");
}
