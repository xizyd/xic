#ifndef RHO_RAILWAY_HPP
#define RHO_RAILWAY_HPP

#include "../Xi/Array.hpp"
#include "../Xi/Crypto.hpp"
#include "../Xi/Func.hpp"
#include "../Xi/Map.hpp"
#include "../Xi/String.hpp"
#include "../Xi/Time.hpp"

namespace Xi {

static const unsigned char RAILWAY_SECURE = 0x01;
static const unsigned char RAILWAY_IS_BROADCAST = 0x02;
static const unsigned char RAILWAY_HAS_META = 0x04;

class Railway {
public:
  struct RailwayPacket {
    u32 channel;
    Xi::String payload;
  };

  struct RailwayChannel {
    Xi::String key;
    Xi::Array<Xi::u8> bitmap;
    Xi::u64 slidePos = 0;
    Xi::u64 lastSentNonce = 0;
    Xi::u64 lastReceivedTime = 0;
    Xi::u64 lastSentTime = 0;
    String lastSentMeta;
    bool updateMeta = false;
    bool isEnabled = false;
    Xi::Map<u64, Xi::String> meta;
  };

  struct ParseResult {
    bool success = false;
    u32 channelID = 0;
    RailwayChannel *channel = nullptr;
    Xi::String payload;
  };

private:
  Xi::Map<u32, RailwayChannel> channels;
  Xi::Array<u32> availableToGenerate;
  int windowBitmapSize = 64;
  u64 destroyTimeout = 17000;
  Xi::Func<void(RailwayChannel *, u32)> clearCallback;

  void generateNewAvailableId() {
    while (true) {
      u32 id = (Xi::random(100000)) + 1;
      if (channels.has(id))
        continue;
      if (availableToGenerate.find(id) != -1)
        continue;
      availableToGenerate.push(id);
      return;
    }
  }

public:
  Railway() {}

  void setWindowBitmap(int size) {
    if (size > 0 && size % 8 == 0)
      windowBitmapSize = size;
  }
  void setClearTimeout(u64 ms) { destroyTimeout = ms; }
  void onClear(Xi::Func<void(RailwayChannel *, u32)> cb) {
    clearCallback = Xi::Move(cb);
  }

  void enable(u32 channelId) {
    RailwayChannel *ch = get(channelId);
    if (ch)
      ch->isEnabled = true;
  }

  int generate() {
    if (availableToGenerate.length == 0)
      generateNewAvailableId();
    return availableToGenerate.pop();
  }

  RailwayChannel *get(int channelId, const Xi::String &key = Xi::String()) {
    if (!channels.has(channelId)) {
      RailwayChannel ch;
      ch.key = key;
      ch.bitmap.alloc(windowBitmapSize / 8);
      for (int i = 0; i < windowBitmapSize / 8; ++i)
        ch.bitmap.push(0);
      ch.lastReceivedTime = Xi::millis();
      channels.put(channelId, ch);
      return channels.get(channelId);
    }
    RailwayChannel *ch = channels.get(channelId);
    if (key.length > 0)
      ch->key = key;
    return ch;
  }

  void remove(int channelId) {
    if (channels.has(channelId)) {
      if (clearCallback.isValid())
        clearCallback(channels.get(channelId), channelId);
      channels.remove(channelId);
    }
  }

  Xi::String build(const RailwayPacket &pkt) {
    RailwayChannel *ch = get(pkt.channel);
    bool isSecure = (ch->key.length == 32);
    u8 header = 0;
    if (isSecure)
      header |= RAILWAY_SECURE;
    if (!ch->isEnabled)
      header |= RAILWAY_IS_BROADCAST;

    Xi::String metaBlob;
    for (auto &it : ch->meta) {
      metaBlob.pushVarLong((long long)it.key);
      metaBlob.pushVarLong(it.value.length);
      metaBlob += it.value;
    }

    Xi::String content;
    if (ch->updateMeta || !metaBlob.constantTimeEquals(ch->lastSentMeta)) {
      header |= RAILWAY_HAS_META;
      content.pushVarLong((long long)metaBlob.length);
      content += metaBlob;
      ch->updateMeta = false;
      ch->lastSentMeta = metaBlob;
    }

    content += pkt.payload;
    Xi::String ad;
    ad.push(header);
    ad.push((pkt.channel >> 16) & 0xFF);
    ad.push((pkt.channel >> 8) & 0xFF);
    ad.push(pkt.channel & 0xFF);

    ch->lastSentTime = Xi::millis();

    if (isSecure) {
      ch->lastSentNonce++;
      Xi::AEADOptions aeo;
      aeo.text = content;
      aeo.ad = ad;
      aeo.tagLength = 8;
      aeadSeal(ch->key, ch->lastSentNonce, aeo);
      Xi::String res = ad;
      res.pushVarLong((long long)ch->lastSentNonce);
      res += aeo.tag;
      res += aeo.text;
      return res;
    }
    return ad + content;
  }

  ParseResult parse(const Xi::String &buf) {
    ParseResult res;
    if (buf.length < 4)
      return res;
    usz at = 0;
    u8 header = buf[at++];
    u32 cid = ((u32)buf[at] << 16) | ((u32)buf[at + 1] << 8) | (u32)buf[at + 2];
    at += 3;

    Xi::String ad = buf.begin(0, 4);
    bool isSecure = (header & RAILWAY_SECURE) != 0;
    bool isBroadcast = (header & RAILWAY_IS_BROADCAST) != 0;
    bool hasMeta = (header & RAILWAY_HAS_META) != 0;

    RailwayChannel *ch = get(cid);
    if (isSecure && ch->key.length != 32)
      return res;
    if (!isBroadcast && !ch->isEnabled)
      return res;

    ch->lastReceivedTime = Xi::millis();

    Xi::String plain;
    if (isSecure) {
      auto nRes = buf.peekVarLong(at);
      if (nRes.error)
        return res;
      u64 nonce = (u64)nRes.value;
      at += nRes.bytes;
      if (at + 8 > buf.length)
        return res;

      if (nonce <= ch->slidePos) {
        u64 diff = ch->slidePos - nonce;
        if (diff >= (u64)windowBitmapSize)
          return res;
        if ((ch->bitmap[(int)(diff / 8)] >> (int)(diff % 8)) & 1)
          return res;
      }

      Xi::AEADOptions aeo;
      aeo.tag = buf.begin(at, at + 8);
      at += 8;
      aeo.text = buf.begin(at, buf.length);
      aeo.ad = ad;
      aeo.tagLength = 8;
      if (!aeadOpen(ch->key, nonce, aeo))
        return res;
      plain = aeo.text;

      if (nonce > ch->slidePos) {
        u64 shift = nonce - ch->slidePos;
        if (shift >= (u64)windowBitmapSize) {
          for (usz i = 0; i < ch->bitmap.length; ++i)
            ch->bitmap[i] = 0;
        } else {
          int bS = (int)(shift / 8), biS = (int)(shift % 8);
          if (bS > 0) {
            for (int i = (int)ch->bitmap.length - 1; i >= bS; --i)
              ch->bitmap[i] = ch->bitmap[i - bS];
            for (int i = 0; i < bS; ++i)
              ch->bitmap[i] = 0;
          }
          if (biS > 0) {
            for (int i = (int)ch->bitmap.length - 1; i > 0; --i)
              ch->bitmap[i] =
                  (ch->bitmap[i] << biS) | (ch->bitmap[i - 1] >> (8 - biS));
            ch->bitmap[0] <<= biS;
          }
        }
        ch->slidePos = nonce;
      }
      ch->bitmap[(int)((ch->slidePos - nonce) / 8)] |=
          (1 << (int)((ch->slidePos - nonce) % 8));
    } else {
      plain = buf.begin(at, buf.length);
    }

    usz pAt = 0;
    if (hasMeta && plain.length > 0) {
      u64 mLen = Encoding::readVarLong(plain, pAt);
      if (pAt + mLen <= plain.length) {
        Xi::String blob = plain.begin(pAt, pAt + (usz)mLen);
        pAt += (usz)mLen;
        usz mAt = 0;
        ch->meta.clear();
        while (mAt < blob.length) {
          u64 k = Encoding::readVarLong(blob, mAt);
          u64 vL = Encoding::readVarLong(blob, mAt);
          if (mAt + vL <= blob.length) {
            ch->meta.put(k, blob.begin(mAt, mAt + (usz)vL));
            mAt += (usz)vL;
          } else
            break;
        }
      }
    }

    res.success = true;
    res.channelID = cid;
    res.channel = ch;
    res.payload = plain.begin(pAt, plain.length);
    return res;
  }

  void update() {
    u64 now = Xi::millis();
    Xi::Array<u32> toRem;
    for (auto &it : channels) {
      if (now - it.value.lastReceivedTime > destroyTimeout)
        toRem.push(it.key);
    }
    for (u32 k : toRem)
      remove(k);
  }
};
} // namespace Xi
#endif