using System;
using System.Collections.Generic;

namespace VhsDecodeAutoAudioAlign.TbcJson
{
    class TbcJsonFixup
    {
        readonly ILog log;

        public TbcJsonFixup(ILog log)
        {
            this.log = log ?? throw new ArgumentNullException(nameof(log));
        }

        public TbcJsonDocument Fixup32BitOverflow(TbcJsonDocument doc)
        {
            return new TbcJsonDocument()
            {
                videoParameters = doc.videoParameters,
                fields = Fix(doc.fields)
            };
        }

        const long RollOver32BitSigned = 0x100000000L;

        TbcJsonDocument.Field[] Fix(TbcJsonDocument.Field[] bad)
        {
            if (bad == null)
                return null;

            long rolloverCorrection = 0;
            var fixedFields = new List<TbcJsonDocument.Field>();
            TbcJsonDocument.Field lastField = null;

            foreach (var f in bad)
            {
                if (f.fileLoc < 0 && (lastField == null || lastField.fileLoc > 0))
                {
                    rolloverCorrection += RollOver32BitSigned;
                    log.That("Fixing singed 32Bit rollover in field " + f.seqNo);
                }

                lastField = f;

                fixedFields.Add(new TbcJsonDocument.Field()
                {
                    fileLoc = f.fileLoc + rolloverCorrection,
                    isFirstField = f.isFirstField,
                    seqNo = f.seqNo
                });
            }

            return fixedFields.ToArray();
        }
    }
}
