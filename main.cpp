#include <QAudioDeviceInfo>
#include <QCoreApplication>
#include <QAudioOutput>
#include <QBuffer>

#include <qmath.h>
#include <qendian.h>

#include <iostream>
#include <qDebug>

#include <stdio.h>      /* printf, scanf, puts, NULL */
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

int getBit(QByteArray & ba, int bit)
{
    if (bit < 0 || bit > ba.size()*8)
        return 1;
    else
        return ba[bit/8] & (1 << bit%8) ? 1: 0;
}

void setBit(QByteArray & ba, int bit, int value)
{
    if (value)
        ba[bit/8] = ba[bit/8] | (1 << bit%8);
    else
        ba[bit/8] = ba[bit/8] & (~(1 << bit%8));
}

int state=0x0005;


int scrambler(int h, int input){
    int y, y2, y5;
    y2 = (0x0008 & h) >> 3;
        y5 = 0x0001 & h;
    y= input ^ y2 ^ y5;
    h = h >> 1;
    y = y << 4;
    state = h | y;
        y = y>>4;
    return y;

}

int descrambler(int h, int input){
    int x, input2, input5;
    input2 = (0x0008 & h) >> 3;
        input5 = 0x0001 & h;
    x= input ^ input2 ^ input5;
    h = h >> 1;
    x = x << 4;
    state = h | x;
        x = x>>4;
    return x;

}
void scramble(QByteArray & ba)
{
    for (int i=0; i< ba.size(); i++) {
        ba[i] = (char)scrambler(state, ba[i]);
    }
}

void descramble (QByteArray & ba)
{
    for (int i=0; i< ba.size(); i++) {
        ba[i] = (char)descrambler(state, ba[i]);
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
        scramble(ba);
        std::cout << "======" << std::endl;
        std::cout << "scrambled:" << std::endl;
        printBa(ba);
        //descramble(ba);
        //std::cout << "======" << std::endl;
        //std::cout << "descrambled:" << std::endl;
        //printBa(ba);

        //wrap(ba);
        //std::cout << "======" << std::endl;
        //std::cout << "wrapped:" << std::endl;
        //printBa(input);


        QByteArray * result = modulator->Modulate(ba);

        //std::cout << "result:" << std::endl;
        //printBa(*result);
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

    srand (time(NULL));


    for (int i=0;i<30;i++)
        input.append(char(0x00));

    //std::cout << "input:" << std::endl;
    //printBa(input);
    //std::cout << "======" << std::endl;


    Modem * m = new Modem(1400,2100,0.01);
    m->emitSignal(&input);

    return a.exec();
}
