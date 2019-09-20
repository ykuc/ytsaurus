package skiff

import (
	"bytes"
	"testing"

	"github.com/stretchr/testify/require"

	"a.yandex-team.ru/library/go/ptr"
)

type TestRow struct {
	First  *int64 `yson:"first"`
	Second int64  `yson:"second"`
	Third  string `yson:"third"`
}

func TestDecoder(t *testing.T) {
	input := bytes.NewBuffer([]byte{
		0x00, 0x00, // first row
		0x01,                                                 // key switch
		0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // row index == 2
		0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // range index == 3
		0x00,                                                 // first field is missing
		0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // second field is present
		0x08, 0x00, 0x00, 0x00, 'a', 'b', 'b', 'a', 'c', 'a', 'b', 'a', // third field is string

		0x00, 0x00, // second row
		0x00,                                                 // no key switch
		0x00,                                                 // no row index
		0x00,                                                 // no range index
		0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // first field is present
		0x00,                   // second field is missing
		0x00, 0x00, 0x00, 0x00, // empty string
	})

	schema := Schema{
		Type: TypeTuple,
		Children: []Schema{
			systemPrefix[0],
			systemPrefix[1],
			systemPrefix[2],
			OptionalColumn("first", TypeInt64),
			OptionalColumn("second", TypeInt64),
			{Type: TypeString32, Name: "third"},
		},
	}

	format := Format{
		Name:           "skiff",
		TableSchemas:   []interface{}{&schema},
		SchemaRegistry: nil,
	}

	decoder, err := NewDecoder(input, format)
	require.NoError(t, err)

	var row TestRow

	require.True(t, decoder.Next())
	require.Equal(t, 0, decoder.TableIndex())
	require.True(t, decoder.KeySwitch())
	require.Equal(t, int64(2), decoder.RowIndex())
	require.Equal(t, 3, decoder.RangeIndex())
	require.NoError(t, decoder.Scan(&row))
	require.Equal(t, row, TestRow{Second: 3, Third: "abbacaba"})

	require.True(t, decoder.Next())
	require.Equal(t, 0, decoder.TableIndex())
	require.False(t, decoder.KeySwitch())
	require.Equal(t, int64(3), decoder.RowIndex())
	require.Equal(t, 3, decoder.RangeIndex())
	require.NoError(t, decoder.Scan(&row))
	require.Equal(t, row, TestRow{First: ptr.Int64(5)})

	require.False(t, decoder.Next())
	require.NoError(t, decoder.Err())
}
