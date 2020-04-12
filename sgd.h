#define SGD_OFFSET  0x94

enum SGDType {
    SGD_NODEBUCK = 4,
    SGD_LEAFBUCK,
    SGD_FILEHEADER = 10,
    SGD_TABLE = 12,
    SGD_NAMETABLE,
    SGD_DESCR,
    SGD_ENUM,
    SGD_HIERARCHY,
    SGD_REC,
    SGD_QTHEAD = 20,
    SGD_QTBUCK,
    SGD_QTPOSTREE,
    SGD_CBIMHEAD,
    SGD_CBIMADRTAB,
    SGD_CBIMTILE,
    SGD_MRCIHEADER,
    SGD_FILEHEAD = 30,
    SGD_0DIMAVR = 34,
    SGD_1DIMAVR,
    SGD_2DIMAVR,
    SGD_POINT2D = 40,
    SGD_HSYM2D,
    SGD_POLYLINE2D = 45,
    SGD_CIRCULARARC2D,
    SGD_ELLIPTICALARC2D,
    SGD_CUBICSPLINE2D,
    SGD_LASSO2D = 50,
    SGD_ELLISEG2D,
    SGD_BOX2D,
    SGD_TEXTLINE2D = 55,
    SGD_TEXTBLOCK2D,
    SGD_SYMREF2D = 58,
    SGD_POINT3D = 60,
    SGD_HSYM3D,
    SGD_POLYLINE3D = 65,
    SGD_CIRCULARARC3D,
    SGD_ELLIPTICALARC3D,
    SGD_CUBICSPLINE3D,
    SGD_LASSO3D = 70,
    SGD_ELLISEG3D,
    SGD_BOX3D,
    SGD_TEXTLINE3D = 75,
    SGD_TEXTBLOCK3D,
    SGD_SYMREF3D = 78,
    SGD_COMPOSEDLINE = 80,
    SGD_COMPCONNECTAREA,
    SGD_SIMPLEAREA,
    SGD_CONNECTEDAREA,
    SGD_COMPOSEDAREA,
    SGD_SEGMENT,
    SGD_RASTER = 87,
    SGD_GRAUBILD,
    SGD_FARBBILD,
    SGD_SET,
    SGD_SEQUENCE,
    SGD_BULKDATA = 99
};

enum SGDBMPType {
    SGD_BMPTILELIST = 0x4ed,
    SGD_BMPTILE = 0x4ee,
    SGD_BMPPALETTE = 0x4ef
};

typedef struct {
    uint32_t    magic1;
    uint16_t    ver_major;
    uint16_t    ver_minor;
    uint32_t    flags;
    uint32_t    magic2;
} SGDFileHeader;

typedef struct {
    uint32_t    num_entries;
    struct {
        uint32_t    type;
        uint32_t    addr;
    } entry[8];
} SGDDirectoryTable;

typedef struct {
    uint16_t    size_16;
    uint16_t    type;
    uint32_t    size;
    uint32_t    unk2;
} SGDDirectoryHeader;

typedef struct {
    SGDDirectoryHeader  hdr;
    uint32_t            num_entries;
    uint32_t            unk4;
    uint32_t            unk5;
    uint32_t            addr[0];
} SGDDirectoryType0;

typedef struct {
    SGDDirectoryHeader  hdr;
    uint32_t            type;
    double              box[8];
    double              scale;
    uint32_t            timestamp;
} SGDDirectoryType1;

typedef struct {
    SGDDirectoryHeader  hdr;
    float               pos[28];
    uint32_t            unk31;
    uint32_t            unk32;
    uint32_t            num_entries;
    uint32_t            addr[0];
} SGDDirectoryType2;

typedef union {
    SGDDirectoryHeader  hdr;
    SGDDirectoryType0   type0;
    SGDDirectoryType1   type1;
    SGDDirectoryType2   type2;
} SGDDirectory;

typedef struct {
    uint16_t    size;
    uint16_t    type;
    uint32_t    index;
    uint32_t    unk2;
    uint32_t    unk3;
    uint32_t    unk4;
    uint32_t    unk5;
    uint32_t    unk6;
} SGDEntryHeader;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        width;
    uint32_t        height;
    uint32_t        unk9;
    uint32_t        unk10;
    uint32_t        unk11;
    uint32_t        unk12;
    uint32_t        unk13;
    uint32_t        unk14;
    float           unk15;
    uint32_t        unk16;
    float           unk17;
    uint32_t        unk18;
    uint32_t        unk19;
    uint32_t        unk20;
    uint32_t        unk21;
    uint32_t        unk22;
    uint32_t        unk23;
    uint32_t        unk24;
    uint32_t        unk25;
    uint32_t        unk26;
    uint32_t        bytes_per_pixel;
    uint32_t        bit_depth;
    uint32_t        palette_addr;
    uint32_t        tile_width;
    uint32_t        tile_height;
    uint32_t        unk32;
    uint32_t        unk33;
    uint32_t        unk34;
    uint32_t        unk35;
    uint32_t        bitmap_addr;
} SGDMrciHeader;

typedef struct {
    uint16_t    size;
    uint16_t    type;
    uint32_t    addr[0];
} SGDMrciBitmap;

typedef struct {
    uint16_t    size;
    uint16_t    type;
    uint32_t    encoding;
    uint8_t     data[0];
} SGDMrciTile;

typedef struct {
    uint16_t    size;
    uint16_t    type;
    uint16_t    bytes_per_pixel;
    uint16_t    bit_depth;
    uint32_t    num_colors;
    uint8_t     data[0];
} SGDMrciPalette;

typedef struct {
    float   x;
    float   y;
} SGDPoint;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        unk7;
    uint32_t        unk8;
    SGDPoint        pos;
    float           unk11;
    SGDPoint        width;
    SGDPoint        height;
    SGDPoint        end;
    char            text[0];
} SGDTextline;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        num_points;
    SGDPoint        points[0];
} SGDLasso;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        point1;
    uint32_t        point2;
    uint32_t        num_points;
    SGDPoint        points[0];
} SGDPolyline;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        unk7;
    uint32_t        unk8;
    uint32_t        num_points;
    SGDPoint        points[0];
} SGDEllipticalArc;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        num_entries;
    int32_t         entries[0];
} SGDSimpleArea;

typedef struct {
    SGDEntryHeader  hdr;
    uint32_t        unk7;
    uint32_t        num_entries;
    uint32_t        entries[0];
} SGDSet;

typedef struct {
    SGDEntryHeader  hdr;
    SGDPoint        point;
    uint32_t        num_entries;
    uint32_t        entries[0];
} SGDPointEntry;

typedef union {
    SGDEntryHeader      hdr;
    SGDMrciHeader       mrci;
    SGDPolyline         polyline;
    SGDEllipticalArc    elliptical_arc;
    SGDLasso            lasso;
    SGDTextline         textline;
    SGDSimpleArea       simple_area;
    SGDSet              set;
    SGDPointEntry       point;
} SGDEntry;
