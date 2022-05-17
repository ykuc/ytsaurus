package ytprof

import (
	"context"
	"fmt"
	"sort"
	"strings"

	"a.yandex-team.ru/yt/go/guid"
	"a.yandex-team.ru/yt/go/migrate"
	"a.yandex-team.ru/yt/go/schema"
	"a.yandex-team.ru/yt/go/ypath"
	"a.yandex-team.ru/yt/go/yt"

	"github.com/google/cel-go/checker/decls"

	exprpb "google.golang.org/genproto/googleapis/api/expr/v1alpha1"
)

const (
	TableMetadata = "metadata"
	TableData     = "data"
	TimeFormat    = "2006-01-02T15:04:05"
)

var (
	SchemaMetadata = schema.MustInfer(&ProfileMetadata{})
	SchemaData     = schema.MustInfer(&ProfileData{})

	Schema = map[string]schema.Schema{
		TableMetadata: SchemaMetadata,
		TableData:     SchemaData,
	}
)

type ProfID struct {
	ProfIDHigh uint64 `yson:",key"`
	ProfIDLow  uint64 `yson:",key"`
}

func ProfIDFromGUID(g guid.GUID) (profID ProfID) {
	profID.ProfIDHigh, profID.ProfIDLow = g.Halves()
	return
}

func GUIDFormProfID(profID ProfID) guid.GUID {
	return guid.FromHalves(profID.ProfIDHigh, profID.ProfIDLow)
}

type Metadata struct {
	MapData map[string]string `yson:",omitempty"`
}

func (m *Metadata) Vars() map[string]interface{} {
	return map[string]interface{}{
		"Metadata": m.MapData,
	}
}

func (m *Metadata) Types() map[string]*exprpb.Type {
	return map[string]*exprpb.Type{
		"Metadata": decls.NewMapType(decls.String, decls.String),
	}
}

type ProfileMetadata struct {
	Timestamp  schema.Timestamp `yson:",key"`
	ProfIDHigh uint64           `yson:",key"`
	ProfIDLow  uint64           `yson:",key"`

	Metadata Metadata `yson:",omitempty"`
}

type ProfileData struct {
	ProfIDHigh uint64 `yson:",key"`
	ProfIDLow  uint64 `yson:",key"`

	Data []byte `yson:",omitempty"`
}

func (s *ProfileMetadata) ProfID() ProfID {
	return ProfID{
		ProfIDHigh: s.ProfIDHigh,
		ProfIDLow:  s.ProfIDLow,
	}
}

func (s *ProfileData) ProfID() ProfID {
	return ProfID{
		ProfIDHigh: s.ProfIDHigh,
		ProfIDLow:  s.ProfIDLow,
	}
}

func (s *ProfileMetadata) String() string {
	var sb strings.Builder

	sb.WriteString(fmt.Sprintf("GUID:%v ", GUIDFormProfID(s.ProfID())))
	sb.WriteString(fmt.Sprintf("Timestamp:%v ", s.Timestamp.Time().Format(TimeFormat)))

	var properties []string
	for key, val := range s.Metadata.MapData {
		properties = append(properties, fmt.Sprintf("%v:%v ", key, val))
	}

	sort.Strings(properties)
	for _, prop := range properties {
		sb.WriteString(prop)
	}

	return sb.String()
}

func MigrateTables(yc yt.Client, root ypath.Path) error {
	tables := map[ypath.Path]migrate.Table{}

	for name, tableSchema := range Schema {
		tables[root.Child(name)] = migrate.Table{
			Schema: tableSchema,
		}
	}

	alter := migrate.OnConflictTryAlter(context.Background(), yc)
	return migrate.EnsureTables(context.Background(), yc, tables, alter)
}
