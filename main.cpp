#include <QAudioDeviceInfo>
#include <QCoreApplication>
#include <QAudioOutput>
#include <QBuffer>

#include <qmath.h>
#include <qendian.h>

#include <iostream>
#include <qDebug>


void scramble(QByteArray & ba)
{
    for (int i=0; i< ba.size(); i++) {
        ba[i] = ~ba[i];
    }
}

void wrap(QByteArray & ba)
{
    ba.push_front((char)0x00);
    ba.push_front((char)0x00);
    ba.push_front((char)0x00);

    ba.push_back((char)0xFF);
}

void printBa(QByteArray & ba)
{
    foreach (char b, ba) {
        std::cout <</* std::hex <<*/ ((unsigned int) b & 0xff) << std::endl;
    }
}

class Modulator
{
    float m_freq_0;
    float m_freq_1;
    float m_bitLength;
    const QAudioFormat & m_format;
public:
    Modulator(float freq0, float freq1, float bitLengthSec, const QAudioFormat &format)
        : m_freq_0(freq0)
        , m_freq_1(freq1)
        , m_bitLength(bitLengthSec)
        , m_format(format)
    {}

    unsigned char * addCount(unsigned char *ptr, qreal x)
    {
        for (int i=0; i<m_format.channelCount(); ++i) {
            if (m_format.sampleSize() == 8 && m_format.sampleType() == QAudioFormat::UnSignedInt) {
                const quint8 value = static_cast<quint8>((1.0 + x) / 2 * 255);
                *reinterpret_cast<quint8*>(ptr) = value;
            } else if (m_format.sampleSize() == 8 && m_format.sampleType() == QAudioFormat::SignedInt) {
                const qint8 value = static_cast<qint8>(x * 127);
                *reinterpret_cast<quint8*>(ptr) = value;
            } else if (m_format.sampleSize() == 16 && m_format.sampleType() == QAudioFormat::UnSignedInt) {
                quint16 value = static_cast<quint16>((1.0 + x) / 2 * 65535);
                if (m_format.byteOrder() == QAudioFormat::LittleEndian)
                    qToLittleEndian<quint16>(value, ptr);
                else
                    qToBigEndian<quint16>(value, ptr);
            } else if (m_format.sampleSize() == 16 && m_format.sampleType() == QAudioFormat::SignedInt) {
                qint16 value = static_cast<qint16>(x * 32767);
                if (m_format.byteOrder() == QAudioFormat::LittleEndian)
                    qToLittleEndian<qint16>(value, ptr);
                else
                    qToBigEndian<qint16>(value, ptr);
            }

        }
        ptr +=  m_format.sampleSize() / 8;
        return ptr;
    }

    unsigned char * generateSin(unsigned char *ptr, float frequency, float seconds)
    {
        for (int i=0; i< seconds*m_format.sampleRate(); i++){
            const qreal x = qSin(2 * M_PI * frequency * i / m_format.sampleRate());
            ptr = addCount(ptr, x);
        }
        return ptr;
    }

    QByteArray * Modulate(QByteArray & ba)
    {
        QByteArray * result = new QByteArray();
        const int channelBytes = m_format.sampleSize() / 8;
        const int sampleBytes = m_format.channelCount() * channelBytes;
        const int outBits = ba.size()*8;
        const int counts = outBits*m_bitLength*m_format.sampleRate();

        result->resize(counts*sampleBytes);
        unsigned char *ptr = reinterpret_cast<unsigned char *>(result->data());
        int temp = (int) ptr;
        int diff = (int) ptr - temp ;
        for (int i=0; i < ba.size(); i++)
        {
            for (int j=0; j<8;j++)
            {
                if (ba[i] & (1 << j))
                    ptr = generateSin(ptr, m_freq_1, m_bitLength);
                else
                    ptr = generateSin(ptr, m_freq_0, m_bitLength);
                diff = int(ptr) - temp ;
            }
        }

        return result;
    }
};

class Modem : public QObject
{
    //Q_OBJECT

    QAudioDeviceInfo m_device;
    QAudioFormat m_format;
    Modulator * modulator;
    QBuffer  m_audioOutputIODevice;

public:
    Modem(float freq_0, float freq_1, float bitLengthSec)
    {
        m_device = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput)[0];

        m_format.setSampleRate(44100);
        m_format.setChannelCount(1);
        m_format.setSampleSize(8);
        m_format.setCodec("audio/pcm");
        m_format.setByteOrder(QAudioFormat::LittleEndian);
        m_format.setSampleType(QAudioFormat::UnSignedInt);

        modulator = new Modulator(freq_0, freq_1, bitLengthSec, m_format);
    }

    void emitSignal(QByteArray * input)
    {
        QByteArray ba(*input);
        //scramble(ba);
        //std::cout << "======" << std::endl;
        //std::cout << "scrambled:" << std::endl;
        //printBa(input);

        //wrap(ba);
        //std::cout << "======" << std::endl;
        //std::cout << "wrapped:" << std::endl;
        //printBa(input);

        QByteArray * result = modulator->Modulate(ba);

        std::cout << "result:" << std::endl;
        printBa(*result);
        m_audioOutputIODevice.close();
        m_audioOutputIODevice.setBuffer(result);
        m_audioOutputIODevice.open(QIODevice::ReadOnly);

        QAudioOutput * m_audioOutput = new QAudioOutput(m_device, m_format, this);
        m_audioOutput->start(&m_audioOutputIODevice);
    }
};


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QByteArray input;

    input.append((char)0xAA);
    /*
    input.append((char)0xFF);
    input.append((char)0x00);
    input.append((char)0xFF);

    std::cout << "input:" << std::endl;
    printBa(input);
    std::cout << "======" << std::endl;
*/

    Modem * m = new Modem(1400,2100,0.01);
    m->emitSignal(&input);

    return a.exec();
}
