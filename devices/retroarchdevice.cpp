#include <QEventLoop>
#include <QLoggingCategory>

#include "retroarchdevice.h"

Q_LOGGING_CATEGORY(log_retroarch, "RETROARCH")
#define sDebug() qCDebug(log_retroarch)

RetroArchDevice::RetroArchDevice(QUdpSocket* sock, QString raVersion, QString gameName, bool snesMemoryMap, bool snesLoromMap)
{
    m_sock = sock;
    m_state = READY;
    sDebug() << "Retroarch device created";
    m_timer = new QTimer();
    m_timer->setInterval(5);
    m_timer->setSingleShot(true);
    dataRead = QByteArray();
    connect(m_timer, SIGNAL(timeout()), this, SLOT(timedCommandDone()));
    connect(m_sock, SIGNAL(readyRead()), this, SLOT(onUdpReadyRead()));
    bigGet = false;
    checkingRetroarch = false;
    hasSnesMemoryMap = snesMemoryMap;
    hasSnesLoromMap = snesLoromMap;
    checkingInfo = false;
    m_raVersion = raVersion;
    m_gameName = gameName;
}


QString RetroArchDevice::name() const
{
    return "EMU RetroArch";
}


USB2SnesInfo RetroArchDevice::parseInfo(const QByteArray &data)
{
    Q_UNUSED(data);
    USB2SnesInfo    info;
    info.romPlaying = m_gameName;
    info.version = m_raVersion;

    if(!hasSnesMemoryMap)
    {
        info.flags << "NO_ROM_ACCESS";
    } else {
        info.flags << "SNES_MEMORY_MAP";

        if(hasSnesLoromMap)
        {
            info.flags << "SNES_LOROM";
        } else {
            info.flags << "SNES_HIROM";
        }
    }

    return info;
}

QList<ADevice::FileInfos> RetroArchDevice::parseLSCommand(QByteArray &dataI)
{
    return QList<ADevice::FileInfos>();
}

bool RetroArchDevice::open()
{
    return m_state != CLOSED;
}

void RetroArchDevice::close()
{
    m_state = CLOSED;
    m_sock->close();
}

// READ_CORE_RAM 200 20
// READ_CORE_RAM 200 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

void RetroArchDevice::onUdpReadyRead()
{
    QByteArray data = m_sock->readAll();
    sDebug() << "<<" << data;
    if (data.isEmpty())
    {
        emit closed();
        return ;
    }
    QList<QByteArray> tList = data.trimmed().split(' ');
    sDebug() << tList;

    if(checkingInfo)
    {
        checkingInfo = false;
        if(tList.at(2) == "-1")
        {
            hasSnesMemoryMap = false;
        } else {
            unsigned char romType = (unsigned char)QByteArray::fromHex(tList.at(23))[0];
            unsigned char romSize = (unsigned char)QByteArray::fromHex(tList.at(24))[0];
            if(romType > 0 && romSize > 0)
            {
                bool loRom = (romType & 1) == 0;
                auto romSpeed = (romType & 0x30);
                if(romSpeed != 0 && tList.at(2) != "00")
                {
                    auto romName = QByteArray::fromHex(tList.mid(2, 21).join()).toStdString();
                    hasSnesMemoryMap = true;
                    hasSnesLoromMap = loRom;
                    m_gameName = QString::fromStdString(romName);
                }
            }
        }

        m_state = READY;
        emit commandFinished();
        return;
    }

    if (tList.at(2) != "-1")
    {
        tList = tList.mid(2);
        data = tList.join();
        sDebug() << "Sending : " << QByteArray::fromHex(data).toHex();
        emit getDataReceived(QByteArray::fromHex(data));
    } else {
        sDebug() << "Not giving data : sending" << lastRCRSize << "bytes";
        emit getDataReceived(QByteArray(lastRCRSize, 0));
    }

    if (bigGet)
    {
        if (sizeRequested == sizeBigGet)
        {
            bigGet = false;
            sizeBigGet = 0;
            sizeRequested = 0;
            addrBigGet = 0;
            m_state  = READY;
            emit commandFinished();
        } else {
            unsigned int mSize = 0;
            if (sizeRequested + 78 <= sizeBigGet)
                mSize = 78;
            else
                mSize = sizeBigGet - sizeRequested;


            addrBigGet += sizePrevBigGet;

            if(hasSnesLoromMap && (addrBigGet < 0x700000 || addrBigGet >= 0x800000))
            {
                if(((addrBigGet&0xFFFF) + sizePrevBigGet) > 0xFFFF)
                {
                    addrBigGet += 0x8000;
                }

                if(((addrBigGet + mSize)&0xFFFF) < 0x8000)
                {
                    mSize = 0x10000 - (addrBigGet&0xFFFF);
                }
            }

            sizePrevBigGet = mSize;
            sizeRequested += mSize;
            read_core_ram(addrBigGet, mSize);
         }
         return;
    }
    emit commandFinished();
    m_state = READY;
}

void RetroArchDevice::read_core_ram(unsigned int addr, unsigned int size)
{
    QByteArray data = "READ_CORE_RAM " + QByteArray::number(addr, 16) + " " + QByteArray::number(size);
    sDebug() << ">>" << data;
    lastRCRSize = size;
    m_sock->write(data);
}

void RetroArchDevice::timedCommandDone()
{
    sDebug() << "Fake cmd finished";
    m_state = READY;
    emit commandFinished();
}

bool RetroArchDevice::hasFileCommands()
{
    return false;
}

bool RetroArchDevice::hasControlCommands()
{
    return false;
}

void RetroArchDevice::fileCommand(SD2Snes::opcode op, QVector<QByteArray> args)
{

}

void RetroArchDevice::fileCommand(SD2Snes::opcode op, QByteArray args)
{

}

void RetroArchDevice::controlCommand(SD2Snes::opcode op, QByteArray args)
{

}

void RetroArchDevice::putFile(QByteArray name, unsigned int size)
{

}

int RetroArchDevice::addr_to_addr(int addr)
{
    if(!hasSnesMemoryMap)
    {
        if (addr >= 0xF50000 && addr <= 0xF70000)
            addr -= 0xF50000;
        else {
            if (addr >= 0xE00000)
                addr = addr - 0xE00000 + 0x20000;
            else
                return -1;
        }
    } else {
        if(addr >= 0xF50000 && addr <= 0xF70000)
        {
            addr -= 0x770000;
        }
        else if(addr >= 0xE00000 && addr <= 0xE10000)
        {
            if(hasSnesLoromMap)
            {
                addr -= 0x700000;
            } else {
                addr -= 0x3FA000;
            }
        }
        else if(addr < 0x700000)
        {
            if(hasSnesLoromMap)
            {
                addr = (addr + (0x8000 * ((addr+0x8000)/0x8000)));
            } else {
                addr = 0x400000;
            }
        }
    }
    return addr;
}

void RetroArchDevice::getAddrCommand(SD2Snes::space space, unsigned int addr, unsigned int size)
{
    sDebug() << "GetAddress " << space << addr << size;
    m_state = BUSY;
    if (space != SD2Snes::SNES)
        return;
    addr = addr_to_addr(addr);
    if (addr == -1)
    {
        emit getDataReceived(QByteArray(size, 0));
        m_timer->start();
        return ;
    }

    if (size > 78)
    {
        bigGet = true;
        sizeBigGet = size;
        sizeRequested = 78;
        size = 78;
        sizePrevBigGet = 78;
        addrBigGet = addr;
    }

    if (hasSnesLoromMap && (addr < 0x700000 || addr >= 0x800000))
    {
        if(((addr+size)&0xFFFF) < 0x8000)
        {
            bigGet = true;
            sizeBigGet = size;
            sizeRequested = 0x10000 - (addr&0xFFFF);
            size = sizeRequested;
            sizePrevBigGet = size;
            addrBigGet = addr;
        }
    }

    read_core_ram(addr, size);
}

void RetroArchDevice::getAddrCommand(SD2Snes::space space, QList<QPair<unsigned int, quint8> > &args)
{

}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, unsigned int addr0, unsigned int size)
{
    int addr = addr0;
    m_state = BUSY;
    if (space != SD2Snes::space::SNES)
        return;
    addr = addr_to_addr(addr);
    if (addr == -1)
        return ;
    sDebug() << "WRITING TO RAM/SRAM" << addr;
    dataToWrite = "WRITE_CORE_RAM " + QByteArray::number(addr, 16) + " ";
}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, QList<QPair<unsigned int, quint8> > &args)
{

}

void RetroArchDevice::putAddrCommand(SD2Snes::space space, unsigned char flags, unsigned int addr, unsigned int size)
{
    putAddrCommand(space, addr, size);
}

void RetroArchDevice::sendCommand(SD2Snes::opcode opcode, SD2Snes::space space, unsigned char flags, const QByteArray &arg, const QByteArray arg2)
{

}

void RetroArchDevice::infoCommand()
{
    sDebug() << "Info command: Checking core memory map status";
    auto tmpData = "READ_CORE_RAM FFC0 32";
    m_sock->write(tmpData);
    checkingInfo = true;
}

void RetroArchDevice::writeData(QByteArray data)
{
    dataToWrite.append(data.toHex(' '));
    sDebug() << "<<" << dataToWrite;
    m_sock->write(dataToWrite);
    m_timer->start();
}
