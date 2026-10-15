#pragma once
#include <QByteArray>
#include <QBuffer>
#include <QImage>
namespace MediaFixture {
inline void u32(QByteArray &out, quint32 value) { for(int i=0;i<4;++i) out.append(char((value>>(i*8))&255)); }
inline void u16(QByteArray &out, quint16 value) { out.append(char(value&255));out.append(char((value>>8)&255)); }
inline QByteArray chunk(const char *tag, QByteArray data) { QByteArray out(tag,4);u32(out,quint32(data.size()));out+=data;if(data.size()%2)out.append('\0');return out; }
inline QByteArray wave() {
    QByteArray format;u16(format,1);u16(format,1);u32(format,8000);u32(format,16000);u16(format,2);u16(format,16);
    QByteArray body="WAVE";body+=chunk("fmt ",format);body+=chunk("data",QByteArray(8000*2*4,'\0'));return chunk("RIFF",body);
}
// Original tiny MJPEG AVI fixture: no download, executable, codec pack or encoder CLI.
inline QByteArray video() {
    constexpr int width=160,height=120,count=40,rate=10; QImage image(width,height,QImage::Format_RGB32);image.fill(QColor(45,120,190));
    QByteArray jpeg;QBuffer buffer(&jpeg);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"JPEG");
    QByteArray avih;u32(avih,1000000/rate);u32(avih,quint32(jpeg.size()*rate));u32(avih,0);u32(avih,0x10);u32(avih,count);u32(avih,0);u32(avih,1);u32(avih,quint32(jpeg.size()));u32(avih,width);u32(avih,height);for(int i=0;i<4;++i)u32(avih,0);
    QByteArray strh="vidsMJPG";u32(strh,0);u16(strh,0);u16(strh,0);u32(strh,0);u32(strh,1);u32(strh,rate);u32(strh,0);u32(strh,count);u32(strh,quint32(jpeg.size()));u32(strh,0xffffffff);u32(strh,0);u16(strh,0);u16(strh,0);u16(strh,width);u16(strh,height);
    QByteArray strf;u32(strf,40);u32(strf,width);u32(strf,height);u16(strf,1);u16(strf,24);strf+="MJPG";u32(strf,width*height*3);for(int i=0;i<4;++i)u32(strf,0);
    QByteArray header="hdrl";header+=chunk("avih",avih);header+=chunk("LIST",QByteArray("strl")+chunk("strh",strh)+chunk("strf",strf));
    QByteArray movie="movi",index;for(int i=0;i<count;++i){index+="00dc";u32(index,0x10);u32(index,quint32(movie.size()));u32(index,quint32(jpeg.size()));movie+=chunk("00dc",jpeg);}
    return chunk("RIFF",QByteArray("AVI ")+chunk("LIST",header)+chunk("LIST",movie)+chunk("idx1",index));
}
}
