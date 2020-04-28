[![Build Status](https://travis-ci.com/keithdoggett/spatial_stats.svg?branch=master)](https://travis-ci.com/keithdoggett/spatial_stats)

# SpatialStats

SpatialStats is an ActiveRecord plugin that utilizes PostGIS and Ruby to compute weights/statistics of spatial data sets in Rails Apps.

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'spatial_stats'
```

And then execute:

```bash
$ bundle
```

Or install it yourself as:

```bash
$ gem install spatial_stats
```

## Usage

### Weights

Weights define the spatial relation between members of a dataset. Contiguous operations are supported for `polygons` and `multipolygons`, and distant operations are supported for `points`.

To compute weights, you need an `ActiveRecord::Relation` scope and a geometry field. From there, you can pick what type of weight operation to compute (`knn`, `queen neighbors`, etc.).

#### Compute Queen Weights

```ruby
# County table has the following fields: avg_income: float, geom: multipolygon.
scope = County.all
geom_field = :geom
weights = SpatialStats::Weights::Contiguous.queen(scope, geom_field)
# => #<SpatialStats::Weights::WeightsMatrix>
```

#### Compute KNN of Centroids

The field being queried does not have to be defined in the schema, but could be computed during the query for scope.

This example finds the inverse distance weighted, 5 nearest neighbors for the centroid of each county.

```ruby
scope = County.all.select("*, st_centroid(geom) as geom")
weights = SpatialStats::Weights::Distant.idw_knn(scope, :geom, 5)
# => #<SpatialStats::Weights::WeightsMatrix>
```

#### Define WeightsMatrix without Query

Weight matrices can be defined by a hash that describes each key's neighbor and weight.

Example: Define WeightsMatrix and get the matrix in row_standardized format.

```ruby
weights = {
    1 => [{ id: 2, weight: 1 }, { id: 4, weight: 1 }],
    2 => [{ id: 1, weight: 1 }],
    3 => [{ id: 4, weight: 1 }],
    4 => [{ id: 1, weight: 1 }, { id: 3, weight: 1 }]
}
keys = weights.keys
wm = SpatialStats::Weights::WeightsMatrix.new(weights)
#  => #<SpatialStats::Weights::WeightsMatrix:0x0000561e205677c0 @keys=[1, 2, 3, 4], @weights={1=>[{:id=>2, :weight=>1}, {:id=>4, :weight=>1}], 2=>[{:id=>1, :weight=>1}], 3=>[{:id=>4, :weight=>1}], 4=>[{:id=>1, :weight=>1}, {:id=>3, :weight=>1}]}, @n=4>

wm = wm.standardize
#  => #<SpatialStats::Weights::WeightsMatrix:0x0000561e205677c0 @keys=[1, 2, 3, 4], @weights={1=>[{:id=>2, :weight=>0.5}, {:id=>4, :weight=>0.5}], 2=>[{:id=>1, :weight=>1}], 3=>[{:id=>4, :weight=>1}], 4=>[{:id=>1, :weight=>0.5}, {:id=>3, :weight=>0.5}]}, @n=4>

wm.dense
# => Numo::DFloat[
#    [0, 0.5, 0, 0.5],
#    [1, 0, 0, 0],
#    [0, 0, 0, 1],
#    [0.5, 0, 0.5, 0]
#   ]

wm.sparse
# => #<SpatialStats::Weights::CSRMatrix @m=4, @n=4, @nnz=6>
```

### Lagged Variables

Spatially lagged variables can be computed with weights matrix and 1-D vector (`Array`).

#### Compute a Lagged Variable

```ruby
weights = {
    1 => [{ id: 2, weight: 1 }, { id: 4, weight: 1 }],
    2 => [{ id: 1, weight: 1 }],
    3 => [{ id: 4, weight: 1 }],
    4 => [{ id: 1, weight: 1 }, { id: 3, weight: 1 }]
}
wm = SpatialStats::Weights::WeightsMatrix.new(weights).standardize
vec = [1, 2, 3, 4]
lagged_var = SpatialStats::Utils::Lag.neighbor_sum(wm, vec)
# => [3.0, 1.0, 4.0, 2.0]
```

### Global Stats

Global stats compute a value for the dataset, like how clustered the observations are within the region.

Most `stat` classes take three parameters: `scope`, `data_field`, and `weights`. All `stat` classes have the `stat` method that will compute the target statistic. These are also aliased with the common name of the statistic, such as `i` for `Moran` or `c` for `Geary`.

#### Compute Moran's I

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Global::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Global::Moran>

moran.stat
# => 0.834

moran.i
# => 0.834
```

#### Compute Moran's I without Querying Data

To calculate the statistic by using an array of data and not querying a database field. The order of the data must correspond to the order of `weights.keys`.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)

field = nil
moran = SpatialStats::Global::Moran.new(scope, field, weights)
# => <SpatialStats::Global::Moran>

# important to standardize the data!
data = [1,2,3,4,5,6].standardize
moran.x = data

moran.stat
# => 0.521
```

#### Compute Moran's I Z-Score

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Global::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Global::Moran>

moran.z_score
# => 3.2
```

#### Run a Permutation Test on Moran's I

All stat classes have the `mc` method which takes `permutations` and `seed` as its parameters. `mc` runs a permutation test on the class and returns the psuedo p-value.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Global::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Global::Moran>

moran.mc(999, 123_456)
# => 0.003
```

#### Get Summary of Permutation Test

All stat classes have the `summary` method which takes `permutations` and `seed` as its parameters. `summary` runs `stat` and `mc` then combines the results into a hash.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Global::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Global::Moran>

moran.summary(999, 123_456)
# => {stat: 0.834, p: 0.003}
```

### Local Stats

Local stats compute a value each observation in the dataset, like how similar its neighbors are to itself. Local stats operate similarly to global stats, except that almost every operation will return an array of length `n` where `n` is the number of observations in the dataset.

Most `stat` classes take three parameters: `scope`, `data_field`, and `weights`. All `stat` classes have the `stat` method that will compute the target statistic. These are also aliased with the common name of the statistic, such as `i` for `Moran` or `c` for `Geary`.

#### Compute Moran's I

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Local::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Local::Moran>

moran.stat
# => [0.888, 0.675, 0.2345, -0.987, -0.42, ...]

moran.i
# => [0.888, 0.675, 0.2345, -0.987, -0.42, ...]
```

#### Compute Moran's I without Querying Data

To calculate the statistic by using an array of data and not querying a database field. The order of the data must correspond to the order of `weights.keys`.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)

field = nil
moran = SpatialStats::Local::Moran.new(scope, field, weights)
# => <SpatialStats::Local::Moran>

# important to standardize the data!
data = [1,2,3,4,5,6].standardize
moran.x = data

moran.stat
# => [0.521, 0.123, -0.432, -0.56,. ...]
```

#### Compute Moran's I Z-Scores

Note: Many classes do not have a variance or expectation method implemented and this will raise a `NotImplementedError`.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Local::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Local::Moran>

moran.z_score
# => # => [0.65, 1.23, 0.42, 3.45, -0.34, ...]
```

#### Run a Permutation Test on Moran's I

All stat classes have the `mc` method which takes `permutations` and `seed` as its parameters. `mc` runs a permutation test on the class and returns the psuedo p-values.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Local::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Local::Moran>

moran.mc(999, 123_456)
# => [0.24, 0.13, 0.53, 0.023, 0.65, ...]
```

#### Get Summary of Permutation Test

All stat classes have the `summary` method which takes `permutations` and `seed` as its parameters. `summary` runs `stat`, `mc`, and `groups` then combines the results into a hash array indexed by `weight.keys`.

```ruby
scope = County.all
weights = SpatialStats::Weights::Contiguous.rook(scope, :geom)
moran = SpatialStats::Local::Moran.new(scope, :avg_income, weights)
# => <SpatialStats::Local::Moran>

moran.summary(999, 123_456)
# => [{key: 1, stat: 0.521, p: 0.24, group: 'HH'}, ...]
```

## Contributing

Once cloned, run the following commands to setup the test database.

```bash
cd ./spatial_stats
bundle install
cd test/dummy
rake db:create
rake db:migrate
```

If you are getting an error, you may need to set the following environment variables.

```bash
$PGUSER # default "postgres"
$PGPASSWORD # default ""
$PGHOST # default "127.0.0.1"
$PGPORT # default "5432"
$PGDATABASE # default "spatial_stats_test"
```

If the dummy app is setup correctly, run the following:

```bash
cd ../..
rake
```

This will run the tests. If they all pass, then your environment is setup correctly.

Note: It is recommended to have GEOS installed and linked to RGeo. You can test this by running the following:

```bash
cd test/dummy
rails c

RGeo::Geos.supported?
# => true
```

## TODO

- ~~Memoize expensive functions within classes~~
- ~~Make star a parameter to getis-ord class~~
- ~~Add examples/usage to docs~~
- ~~Create RDocs~~
- ~~Refactor Global Moran and BVMoran~~
- ~~Support non-numeric keys in WeightsMatrix/General refactor~~
- ~~Write SparseMatrix C ext~~
- ~~Change instances of `standardized` and `windowed` to `standardize` and `window`, respectively.~~
- ~~Add `positive` and `negative` groups for `GetisOrd` and `Geary`, similar to how `#quads` is implemented.~~
- ~~Add `#summary` method to statistics that will combine stat vals with p-vals, and quads or hot/cold spot info.~~
- ~~Add ability to assign `x` or `z` on stat classes so users are not forced to query data to input it into models. Add example to README.~~

## Future Work

#### General

- ~~Refactor stats to inherit an abstract class.~~
- ~~Change WeightsMatrix class and Stat classes to utilize sparse matrix methods.~~
- Split into two separate gems spatial_stats and spatial_stats-activerecord

#### Weights

- Add Kernel based weighting

#### Utils

- Rate smoothing
- Bayes smoothing

#### Global

- Geary class
- GetisOrd class

#### Local

- Join Count Statistic

#### PPA

- Add descriptive stat methods for point clusters.

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).
