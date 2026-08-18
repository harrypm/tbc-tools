/************************************************************************

    vitcffmetadata.h

    tbc-export-metadata - Export ld-decode metadata into other formats
    Copyright (C) 2026 Simon Inns

    This file is part of tbc-tools.

    tbc-export-metadata is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#ifndef VITCFFMETADATA_H
#define VITCFFMETADATA_H

#include <QString>

#include "lddecodemetadata.h"

/*!
    Write all available VITC values in FFmpeg metadata=print style text.

    This mirrors FFmpeg readvitc filter key formatting:
      - frame / pts / pts_time summary line
      - lavfi.readvitc.found
      - lavfi.readvitc.tc_str (when a valid VITC value is available)

    Returns true on success, false on failure.
*/
bool writeVitcFfmetadataText(LdDecodeMetaData &metaData, const QString &fileName);

#endif // VITCFFMETADATA_H
